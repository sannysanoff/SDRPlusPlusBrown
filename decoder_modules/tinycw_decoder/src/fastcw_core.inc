/* Resident CW decoder. Input [B,1,65,T], output [ceil(T/2),B,48].
 * Build: cc -O3 -std=c11 fastcw/fastcw.c -lm -o fastcw/fastcw
 *
 * Pipeline per batch item:
 *   1. carrier c1 = the bin with the best learned score (6-8-1 network on
 *      per-bin time quantiles: brightness, keying contrast, noise floor,
 *      distance from the conditioner center); the spectrogram gets PAD floor
 *      rows on both sides so the key patch fits at a band edge;
 *   2. key MLP (65-24-24-4) on a normalized 7-bin x 9-frame patch gives the
 *      key-on logit of four 3.75 ms sub-frames per 15 ms frame;
 *   3. partner station: the strongest bin within +-8 bins (the same bin
 *      allowed) during c1 silence. It needs >= 4 marks and a mark/gap level
 *      contrast >= 2 gap-level std during c1 silence. On a separate bin (>= 3 away) it must also
 *      be (almost) silent while c1 transmits (turn-taking); QRM keys
 *      regardless and is rejected;
 *   4. transmissions (split at source changes and >= 1 s pauses) each get a
 *      tempo from a 60-candidate search over mark (and half-weight gap) lengths;
 *   5. the key sequence is resampled to 3 steps per local dot; a dilated
 *      depthwise-separable CNN (look-back and look-ahead +-22 dots)
 *      outputs 48-class CTC log probabilities per step;
 *   6. steps are mapped back to the ceil(T/2) output frames.
 * All NW trainable coefficients live in one array loaded once at startup.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#ifndef _WIN32
#include <semaphore.h>   /* only for the shm/sem server below, never in the module */
#endif
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define F 65
#define C 48
#define BW 3                      /* patch half-height in bins */
#define TW 4                      /* patch half-width in frames */
#define D ((2*BW+1)*(2*TW+1)+2)   /* MLP input size: patch + local peak + scale */
#define H 24
#define SUB 4                     /* key sub-frames per input frame */
#define LOCAL_W 67                /* local-peak half window in frames (~1 s) */
#define NMLP (H*D+H+H*H+H+SUB*H+SUB)
#define NDEC 8
#define CH 36                     /* CNN channels */
#define NBLK 6
#define KW 5                      /* CNN kernel width */
#define SPD 3                     /* CNN steps per dot */
#define NCNN (CH*3*KW+CH+NBLK*(CH*KW+CH+CH*CH+CH)+C*CH+C)
#define NCF 6                     /* carrier scorer features per bin */
#define NCH 8                     /* carrier scorer hidden units */
#define NCAR (NCH*NCF+NCH+NCH+1)
#define CAR_UNKEYED 2.0f          /* carrier fallback thresholds, see carrier_learned() */
#define CAR_KEYED 6.0f
#define CAR_SPAN 3.5f
#define NW (NMLP+NDEC+NCNN+NCAR)
#define NTEMPO 60
#define TEMPO_MIN 4.5             /* dot length range in sub-frames */
#define TEMPO_MAX 42.0
#define SUB_S (0.015/SUB)
#define PARTNER_BINS 8            /* +-100 Hz */
#define PARTNER_MIN_SEP 3
#define PARTNER_RATIO .25         /* partner duty inside c1 transmissions / outside */
#define PARTNER_MIN_MARKS 4
#define PARTNER_CONTRAST 2.0      /* (partner mark level - gap level) / gap level std */
#define PAD (BW+1)                /* floor rows added above and below the band */
#define FP (F+2*PAD)
#define JOIN_S .5                 /* c1 marks closer than this form one transmission */
#define SPLIT_S 1.0               /* pause that splits transmissions for tempo */
#define MAX_B 16
#define MAX_T 4096
#define MAX_S (MAX_T*SUB)
#define MAX_STEP (MAX_S*SPD*2/9+2)        /* MAX_S*SPD/TEMPO_MIN, TEMPO_MIN = 4.5 */
#define SHM_NAME "/tinycw_fastcw_v1"
#define REQ_NAME "/tinycw_fastcw_req_v1"
#define DONE_NAME "/tinycw_fastcw_done_v1"
#define LOCK_NAME "/tinycw_fastcw_lock_v1"

/* tempo-search settings stored after the MLP coefficients (slots 0, 5-7 unused) */
enum { P_TH, P_RAMP, P_SIGMA, P_GAP_W, P_CAP };
static const int dilation[NBLK]={1,2,4,8,16,1};

typedef struct {
    volatile int state, batch, time, error;
    float input[MAX_B*F*MAX_T];
    float output[((MAX_T+1)/2)*MAX_B*C];
} Shared;
typedef struct { float w[NW]; } Model;
typedef struct { int on, len; } Run;
typedef struct { int a, b, src; } Span;

static volatile sig_atomic_t stopping=0;
static sem_t *server_request_sem=NULL;
static void stop_handler(int signal_number) {
    (void)signal_number;stopping=1;
    if(server_request_sem)sem_post(server_request_sem);
}

/* k-th smallest value (0-based); partially reorders a */
static float select_kth(float *a,int n,int k) {
    int lo=0,hi=n-1;
    while(lo<hi) {
        float pivot=a[(lo+hi)/2];int i=lo,j=hi;
        while(i<=j) {
            while(a[i]<pivot)i++;
            while(a[j]>pivot)j--;
            if(i<=j){float t=a[i];a[i]=a[j];a[j]=t;i++;j--;}
        }
        if(k<=j)hi=j;else if(k>=i)lo=i;else return a[k];
    }
    return a[k];
}

/* numpy.quantile with linear interpolation, without a full sort */
static float quantile(float *a,int n,double q) {
    double pos=q*(n-1);int lo=(int)floor(pos);
    float low=select_kth(a,n,lo);
    if(lo>=n-1)return low;
    float next=a[lo+1];   /* after selection, a[lo+1..] are all >= low */
    for(int i=lo+2;i<n;i++)if(a[i]<next)next=a[i];
    return (float)(low+(next-low)*(pos-lo));
}

/* Best row in [lo,hi) by the 98th percentile over frames with mask[t] (all if NULL). */
static int carrier_bin(const float *spec,int T,int lo,int hi,const unsigned char *mask) {
    float scratch[MAX_T],best=-INFINITY;int winner=-1;
    for(int f=lo;f<hi;f++) {
        int n=0;const float *row=spec+(size_t)f*T;
        for(int t=0;t<T;t++)if(!mask||mask[t])scratch[n++]=row[t];
        if(!n)continue;
        float q=quantile(scratch,n,.98);
        if(q>best){best=q;winner=f;}
    }
    return winner;
}

/* Main carrier (unpadded bin), see reference.main_carrier():
 * best scorer output; if that bin (+-1) is unkeyed with a small level span
 * while another bin is clearly keyed, the most keyed bin; then the brightest
 * of the bin and its neighbours. */
static int carrier_learned(const Model *m,const float *spec,int T) {
    float scratch[MAX_T],q05[F],q30[F],q50[F],q98[F],tmp[F],x[NCF];
    const float *w1=m->w+NMLP+NDEC+NCNN,*b1=w1+NCH*NCF,*w2=b1+NCH,*b2=w2+NCH;
    float top=-INFINITY;
    for(int f=0;f<F;f++) {
        memcpy(scratch,spec+(size_t)f*T,T*sizeof(float));
        q05[f]=quantile(scratch,T,.05);q30[f]=quantile(scratch,T,.3);
        q50[f]=quantile(scratch,T,.5);q98[f]=quantile(scratch,T,.98);
        if(q98[f]>top)top=q98[f];
    }
    memcpy(tmp,q50,sizeof q50);
    float med=quantile(tmp,F,.5);
    int best=0,zbest=0;float best_score=-INFINITY,zk[F];
    for(int f=0;f<F;f++) {
        float z=(q98[f]-q30[f])/(q30[f]-q05[f]+1e-3f);
        zk[f]=fmaxf(z,1e-3f);if(zk[f]>zk[zbest])zbest=f;
        float up=q98[f+2<F?f+2:F-1],down=q98[f-2>=0?f-2:0];
        x[0]=q98[f]-top;x[1]=logf(fmaxf(z,1e-3f));x[2]=q98[f]-q50[f];
        x[3]=q98[f]-.5f*(up+down);x[4]=q50[f]-med;x[5]=fabsf((float)(f-16))/16.f;
        float score=b2[0];
        for(int h=0;h<NCH;h++) {
            float a=b1[h];for(int i=0;i<NCF;i++)a+=w1[h*NCF+i]*x[i];
            score+=w2[h]*tanhf(a);
        }
        if(score>best_score){best_score=score;best=f;}
    }
    float znear=0;
    for(int f=best-1;f<=best+1;f++)if(f>=0&&f<F&&zk[f]>znear)znear=zk[f];
    if(znear<CAR_UNKEYED&&zk[zbest]>=CAR_KEYED&&q98[best]-q05[best]<CAR_SPAN)best=zbest;
    int lo=best>0?best-1:0,hi=best+1<F?best+1:F-1,c=lo;
    for(int f=lo;f<=hi;f++)if(q98[f]>q98[c])c=f;
    return c;
}

/* Key-on logit for every sub-frame at carrier c: logit[t*SUB+k].
 * Level normalization uses frames with mask[t] when at least 20 are set. */
static void key_logits(const Model *m,const float *spec,int T,int c,const unsigned char *mask,float *logit) {
    float trace[MAX_T],ref[MAX_T],local[MAX_T];int n=0,count=0;
    for(int t=0;t<T;t++) {
        trace[t]=fmaxf(spec[(c-1)*T+t],fmaxf(spec[c*T+t],spec[(c+1)*T+t]));
        if(mask&&mask[t])count++;
    }
    for(int t=0;t<T;t++)if(!mask||count<20||mask[t])ref[n++]=trace[t];
    int i_floor=n/5,i_peak=(int)(n*.97);
    float peak=select_kth(ref,n,i_peak);
    float floor_level=select_kth(ref,i_peak,i_floor);   /* ref[0..i_peak) holds the smaller values */
    float scale=fmaxf(peak-floor_level,.25f),inv=1.f/scale;
    for(int t=0;t<T;t++) {
        int a=t-LOCAL_W<0?0:t-LOCAL_W,b=t+LOCAL_W>=T?T-1:t+LOCAL_W;
        float v=trace[a];for(int k=a+1;k<=b;k++)if(trace[k]>v)v=trace[k];
        local[t]=(v-floor_level)*inv;
    }
    const float *w1=m->w,*b1=w1+H*D,*w2=b1+H,*b2=w2+H*H,*w3=b2+H,*b3=w3+SUB*H;
    float x[D],h1[H],h2[H];
    for(int t=0;t<T;t++) {
        int k0=0;
        for(int f=c-BW;f<=c+BW;f++) {
            const float *row=spec+(size_t)f*T;
            for(int k=-TW;k<=TW;k++) {
                int u=t+k;if(u<0)u=0;if(u>=T)u=T-1;
                x[k0++]=(row[u]-floor_level)*inv;
            }
        }
        x[k0++]=local[t];x[k0++]=scale;
        for(int h=0;h<H;h++) {
            const float *r=w1+h*D;float a[8]={0};int i=0;
            for(;i+8<=D;i+=8)for(int k=0;k<8;k++)a[k]+=r[i+k]*x[i+k];   /* vectorizable */
            float s=b1[h]+((a[0]+a[4])+(a[1]+a[5]))+((a[2]+a[6])+(a[3]+a[7]));
            for(;i<D;i++)s+=r[i]*x[i];
            h1[h]=tanhf(s);
        }
        for(int h=0;h<H;h++) {
            float s=b2[h];const float *r=w2+h*H;
            for(int i=0;i<H;i++)s+=r[i]*h1[i];
            h2[h]=tanhf(s);
        }
        for(int k=0;k<SUB;k++) {
            float s=b3[k];const float *r=w3+k*H;
            for(int i=0;i<H;i++)s+=r[i]*h2[i];
            logit[t*SUB+k]=s;
        }
    }
}

/* ---------------- tempo search (Morse timing model) ---------------- */

/* Cost of explaining the runs with dot length d: every mark against a dot
 * (d - ramp) or a dash (3d - ramp) in log length; every inner gap against an
 * element, character or word gap, weighted by P_GAP_W. Conditioned off-air
 * signals have short gaps, so marks carry most of the evidence. */
static double tempo_cost(const Run *r,int n,double d,const float *p) {
    double ramp=p[P_RAMP],s2=2.0*p[P_SIGMA]*p[P_SIGMA],cap=p[P_CAP],gw=p[P_GAP_W];
    double mdot=log(fmax(d-ramp,.7)),mdash=log(3*d-ramp);
    double g1=log(d+ramp),g3=log(3*d+ramp),g7=log(7*d+ramp),cost=0;
    for(int k=0;k<n;k++) {
        double l=log((double)r[k].len);
        if(r[k].on) {
            double a=(l-mdot)*(l-mdot),b=(l-mdash)*(l-mdash);
            cost+=fmin(fmin(a,b)/s2,cap);
        } else if(k>0&&k<n-1&&gw>0) {
            double e1=(l-g1)*(l-g1),e3=(l-g3)*(l-g3),e7=l>g7?0:(l-g7)*(l-g7);
            cost+=gw*fmin(fmin(e1,fmin(e3,e7))/s2,cap);
        }
    }
    return cost;
}

/* Best dot length (sub-frames) for logits key[0..S); 0 if there is no mark. */
static double tempo_search(const float *key,int S,const float *p) {
    static Run runs[MAX_S];
    int n=0,any=0;
    for(int s=0;s<S;) {
        int on=key[s]>0,e=s+1;
        while(e<S&&(key[e]>0)==on)e++;
        runs[n++]=(Run){on,e-s};any|=on;s=e;
    }
    if(!any)return 0;
    double best=INFINITY,dbest=0;
    for(int i=0;i<NTEMPO;i++) {
        double d=exp(log(TEMPO_MIN)+(log(TEMPO_MAX)-log(TEMPO_MIN))*i/(NTEMPO-1));
        double cost=tempo_cost(runs,n,d,p);
        if(cost<best){best=cost;dbest=d;}
    }
    return dbest;
}

/* ---------------- front-end: carriers, partner test, local tempo ---------------- */

/* Spans of on[] (length S), joining gaps shorter than join. Returns count. */
static int on_spans(const unsigned char *on,int S,int join,Span *out) {
    int n=0;
    for(int s=0;s<S;) {
        if(!on[s]){s++;continue;}
        int e=s+1;while(e<S&&on[e])e++;
        if(n&&s-out[n-1].b<join)out[n-1].b=e;else out[n++]=(Span){s,e,0};
        s=e;
    }
    return n;
}

static int count_marks(const float *key,int a,int b) {
    int n=0;for(int s=a;s<b;s++)if(key[s]>0&&(s==a||key[s-1]<=0))n++;return n;
}

/* Merged key logits and local dot length per sub-frame; spec is padded [FP][T]. */
static void front_end(const Model *m,const float *spec,int T,float *key,double *dot) {
    static float k2[MAX_S];static unsigned char on[MAX_S],inside[MAX_S],src[MAX_S],quiet[MAX_T];
    static Span tx[MAX_S],seg[MAX_S];
    const float *p=m->w+NMLP;int S=T*SUB;
    int c1=PAD+carrier_learned(m,spec+(size_t)PAD*T,T);
    key_logits(m,spec,T,c1,NULL,key);
    for(int s=0;s<S;s++){on[s]=key[s]>0;inside[s]=0;}
    int ntx=on_spans(on,S,(int)(JOIN_S/SUB_S),tx);
    for(int i=0;i<ntx;i++)for(int s=tx[i].a;s<tx[i].b;s++)inside[s]=1;
    int nquiet=0;
    for(int t=0;t<T;t++) {
        int any=0;for(int k=0;k<SUB;k++)any|=inside[t*SUB+k];
        quiet[t]=!any;nquiet+=quiet[t];
    }
    int accepted=0;
    int lo=c1-PARTNER_BINS<PAD?PAD:c1-PARTNER_BINS,hi=c1+PARTNER_BINS+1>PAD+F?PAD+F:c1+PARTNER_BINS+1;
    if(nquiet>=20) {
        int c2=carrier_bin(spec,T,lo,hi,quiet);
        if(c2>=0) {
            key_logits(m,spec,T,c2,quiet,k2);
            long in_on=0,in_n=0,out_on=0,out_n=0;int marks_out=0;double lev_on=0,lev_off=0,lev_off2=0;
            for(int s=0;s<S;s++) {
                int o=k2[s]>0;
                if(inside[s]){in_on+=o;in_n++;}
                else {
                    int t=s/SUB;
                    float v=fmaxf(spec[(c2-1)*T+t],fmaxf(spec[c2*T+t],spec[(c2+1)*T+t]));
                    out_on+=o;out_n++;
                    if(o)lev_on+=v;else{lev_off+=v;lev_off2+=(double)v*v;}
                    if(o&&(s==0||!(k2[s-1]>0)||inside[s-1]))marks_out++;
                }
            }
            double d_in=in_n?(double)in_on/in_n:0,d_out=out_n?(double)out_on/out_n:0;
            long n_off=out_n-out_on;double contrast=0;
            if(out_on&&n_off>1) {
                double mean_off=lev_off/n_off,var=lev_off2/n_off-mean_off*mean_off;
                contrast=(lev_on/out_on-mean_off)/(sqrt(var>0?var:0)+1e-6);
            }
            accepted=marks_out>=PARTNER_MIN_MARKS&&contrast>=PARTNER_CONTRAST;
            if(abs(c2-c1)>=PARTNER_MIN_SEP)accepted=accepted&&d_in<PARTNER_RATIO*d_out;
            if(accepted)for(int s=0;s<S;s++)if(!inside[s])key[s]=k2[s];
        }
    }
    /* transmissions: split at >= SPLIT_S pauses and at source changes */
    for(int s=0;s<S;s++){on[s]=key[s]>0;src[s]=(inside[s]||!accepted)?1:2;}
    int nspan=on_spans(on,S,(int)(SPLIT_S/SUB_S),tx),nseg=0;
    for(int i=0;i<nspan;i++) {
        int s0=tx[i].a;
        for(int t=tx[i].a+1;t<tx[i].b;t++)
            if(src[t]!=src[t-1]&&on[t]){seg[nseg++]=(Span){s0,t,src[s0]};s0=t;}
        seg[nseg++]=(Span){s0,tx[i].b,src[s0]};
    }
    double global=tempo_search(key,S,p);if(global<=0)global=12.0;
    for(int s=0;s<S;s++)dot[s]=global;
    for(int i=0;i<nseg;i++) {
        if(count_marks(key,seg[i].a,seg[i].b)<4)continue;
        double d=tempo_search(key+seg[i].a,seg[i].b-seg[i].a,p);
        if(d>0)for(int s=seg[i].a;s<seg[i].b;s++)dot[s]=d;
    }
}

/* ---------------- tempo-normalized CNN ---------------- */

/* Resample to SPD steps per local dot: x[3][n]; edge[k] = sub-frame position of step k. */
static int resample(const float *key,const double *dot,int S,float *x,double *edge) {
    static double tau[MAX_S+1],cp[MAX_S+1],cl[MAX_S+1];
    tau[0]=cp[0]=cl[0]=0;
    for(int s=0;s<S;s++) {
        float lg=fmaxf(-8.f,fminf(8.f,key[s])),pr=1.f/(1.f+expf(-lg));
        tau[s+1]=tau[s]+SPD/dot[s];cp[s+1]=cp[s]+pr;cl[s+1]=cl[s]+lg;
    }
    int n=(int)tau[S];if(n>MAX_STEP-1)n=MAX_STEP-1;
    int s=0;
    for(int k=0;k<=n;k++) {
        while(s<S-1&&tau[s+1]<k)s++;
        double den=tau[s+1]-tau[s];
        edge[k]=s+(den>0?(k-tau[s])/den:0);
    }
    for(int k=0;k<n;k++) {
        double a=edge[k],b=edge[k+1];
        int ia=(int)a,ib=(int)b;if(ia>S-1)ia=S-1;if(ib>S-1)ib=S-1;
        double ca=cp[ia]+(a-ia)*(cp[ia+1]-cp[ia]),cb=cp[ib]+(b-ib)*(cp[ib+1]-cp[ib]);
        double la=cl[ia]+(a-ia)*(cl[ia+1]-cl[ia]),lb=cl[ib]+(b-ib)*(cl[ib+1]-cl[ib]);
        int mid=(int)((a+b)/2);if(mid>S-1)mid=S-1;
        x[k]=(float)((cb-ca)/(b-a));
        x[MAX_STEP+k]=(float)((lb-la)/(b-a)/8);
        x[2*MAX_STEP+k]=(float)(log(dot[mid])-2.5);
    }
    return n;
}

/* CNN over n steps; x rows are strided by MAX_STEP. Writes log probs lp[k*C+c]. */
static void cnn(const Model *m,const float *x,int n,float *lp) {
    static float a[CH*MAX_STEP],h[CH*MAX_STEP];
    const float *w=m->w+NMLP+NDEC;
    const float *wi=w,*bi=wi+CH*3*KW;w=bi+CH;
    for(int o=0;o<CH;o++)for(int t=0;t<n;t++) {
        float s=bi[o];
        for(int i=0;i<3;i++)for(int j=0;j<KW;j++) {
            int u=t+j-KW/2;if(u>=0&&u<n)s+=wi[(o*3+i)*KW+j]*x[i*MAX_STEP+u];
        }
        a[o*MAX_STEP+t]=s;
    }
    for(int l=0;l<NBLK;l++) {
        const float *dw=w,*db=dw+CH*KW,*pw=db+CH,*pb=pw+CH*CH;w=pb+CH;
        int dil=dilation[l];
        for(int c=0;c<CH;c++) {
            const float *row=a+c*MAX_STEP;float *out=h+c*MAX_STEP;
            for(int t=0;t<n;t++) {
                float s=db[c];
                for(int j=0;j<KW;j++){int u=t+(j-KW/2)*dil;if(u>=0&&u<n)s+=dw[c*KW+j]*row[u];}
                out[t]=s>0?s:0;
            }
        }
        for(int o=0;o<CH;o++) {
            float *row=a+o*MAX_STEP;
            for(int t=0;t<n;t++)row[t]+=pb[o];
            for(int c=0;c<CH;c++) {
                float wv=pw[o*CH+c];const float *hc=h+c*MAX_STEP;
                for(int t=0;t<n;t++)row[t]+=wv*hc[t];
            }
        }
    }
    const float *wo=w,*bo=wo+C*CH;
    for(int c=0;c<CH;c++){float *row=a+c*MAX_STEP;for(int t=0;t<n;t++)if(row[t]<0)row[t]=0;}
    for(int t=0;t<n;t++) {
        float *q=lp+(size_t)t*C,max=-INFINITY;
        for(int k=0;k<C;k++) {
            float s=bo[k];for(int c=0;c<CH;c++)s+=wo[k*CH+c]*a[c*MAX_STEP+t];
            q[k]=s;if(s>max)max=s;
        }
        float sum=0;for(int k=0;k<C;k++)sum+=expf(q[k]-max);
        float norm=max+logf(sum);for(int k=0;k<C;k++)q[k]-=norm;
    }
}

static void infer(const Model *m,const float *input,float *output,int B,int T) {
    static float key[MAX_S],x[3*MAX_STEP],lp[(size_t)MAX_STEP*C],padded[(size_t)FP*MAX_T],all[(size_t)F*MAX_T];
    static double dot[MAX_S],edge[MAX_STEP+1];
    int O=(T+1)/2,S=T*SUB;
    for(int b=0;b<B;b++) {
        const float *spec=input+(size_t)b*F*T;
        memcpy(all,spec,(size_t)F*T*sizeof(float));
        float fill=quantile(all,F*T,.2);
        for(size_t i=0;i<(size_t)PAD*T;i++)padded[i]=padded[(size_t)(PAD+F)*T+i]=fill;
        memcpy(padded+(size_t)PAD*T,spec,(size_t)F*T*sizeof(float));
        front_end(m,padded,T,key,dot);
        int n=resample(key,dot,S,x,edge);
        if(n>0)cnn(m,x,n,lp);
        /* each output frame covers sub-frames [2*SUB*j, 2*SUB*(j+1)); take the
         * step centered in it with the least blank, else the step covering its center */
        int k=0;
        for(int j=0;j<O;j++) {
            float *q=output+((size_t)j*B+b)*C;
            double lo=2.0*SUB*j,hi=lo+2.0*SUB;
            int pick=-1;
            while(k<n&&(edge[k]+edge[k+1])/2<lo)k++;
            for(int i=k;i<n&&(edge[i]+edge[i+1])/2<hi;i++)
                if(pick<0||lp[(size_t)i*C]<lp[(size_t)pick*C])pick=i;
            if(pick<0)for(int i=k>0?k-1:0;i<n;i++)if(edge[i]<=lo+SUB&&edge[i+1]>lo+SUB){pick=i;break;}
            if(pick<0){const float normal=log1pf((C-1)*expf(-16.f));for(int c=0;c<C;c++)q[c]=-16.f-normal;q[0]=-normal;continue;}
            memcpy(q,lp+(size_t)pick*C,C*sizeof(float));
        }
    }
}

static int load_model(Model *m,const char *path) {
    FILE *fp=fopen(path,"rb");if(!fp)return -1;
    size_t count=fread(m->w,sizeof(float),NW,fp);
    int extra=fgetc(fp)!=EOF;fclose(fp);
    if(count!=NW||extra){errno=EINVAL;return -1;}
    return 0;
}

#ifndef FASTCW_NO_MAIN   /* fastcw_wav.c includes this file with its own main */
static int serve(const char *weights) {
    static Model m;if(load_model(&m,weights)){perror(weights);return 1;}
    shm_unlink(SHM_NAME);sem_unlink(REQ_NAME);sem_unlink(DONE_NAME);sem_unlink(LOCK_NAME);
    int fd=shm_open(SHM_NAME,O_CREAT|O_RDWR,0600);if(fd<0){perror("shm_open");return 1;}
    if(ftruncate(fd,sizeof(Shared))){perror("ftruncate");return 1;}
    Shared *sh=mmap(NULL,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    sem_t *req=sem_open(REQ_NAME,O_CREAT,0600,0),*done=sem_open(DONE_NAME,O_CREAT,0600,0);
    sem_t *lock=sem_open(LOCK_NAME,O_CREAT,0600,1);
    if(sh==MAP_FAILED||req==SEM_FAILED||done==SEM_FAILED||lock==SEM_FAILED){perror("shared IPC");return 1;}
    server_request_sem=req;signal(SIGINT,stop_handler);signal(SIGTERM,stop_handler);
    fprintf(stderr,"ready: %d coefficients, resident process, shared buffer\n",NW);fflush(stderr);
    for(;;) {
        while(sem_wait(req)&&errno==EINTR){}
        if(stopping)break;
        if(sh->state==-1)break;
        int B=sh->batch,T=sh->time;
        sh->error=(B<1||B>MAX_B||T<2||T>MAX_T);
        if(!sh->error)infer(&m,sh->input,sh->output,B,T);
        sh->state=2;sem_post(done);
    }
    munmap(sh,sizeof(Shared));close(fd);sem_close(req);sem_close(done);sem_close(lock);
    shm_unlink(SHM_NAME);sem_unlink(REQ_NAME);sem_unlink(DONE_NAME);sem_unlink(LOCK_NAME);return 0;
}

static int client(const char *in,const char *out,int B,int T) {
    if(B<1||B>MAX_B||T<2||T>MAX_T)return 1;
    int fd=shm_open(SHM_NAME,O_RDWR,0);if(fd<0){perror("server not running");return 1;}
    Shared *sh=mmap(NULL,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    sem_t *req=sem_open(REQ_NAME,0),*done=sem_open(DONE_NAME,0),*lock=sem_open(LOCK_NAME,0);
    if(sh==MAP_FAILED||req==SEM_FAILED||done==SEM_FAILED||lock==SEM_FAILED){perror("shared IPC");return 1;}
    FILE *fp=fopen(in,"rb");if(!fp){perror(in);return 1;}
    while(sem_wait(lock)&&errno==EINTR){}
    size_t n=(size_t)B*F*T;if(fread(sh->input,sizeof(float),n,fp)!=n){fprintf(stderr,"short input\n");fclose(fp);sem_post(lock);return 1;}fclose(fp);
    sh->batch=B;sh->time=T;sh->state=1;sem_post(req);
    while(sem_wait(done)&&errno==EINTR){}
    if(sh->error){fprintf(stderr,"server rejected shape\n");sem_post(lock);return 1;}
    fp=fopen(out,"wb");if(!fp){perror(out);sem_post(lock);return 1;}   /* never leave the lock held */
    size_t count=(size_t)((T+1)/2)*B*C;int ok=fwrite(sh->output,sizeof(float),count,fp)==count;
    fclose(fp);sem_post(lock);munmap(sh,sizeof(Shared));close(fd);sem_close(req);sem_close(done);sem_close(lock);return ok?0:1;
}

/* Binary persistent client protocol: uint32 B,T; B*65*T float32 input;
 * returns ceil(T/2)*B*48 float32. One stream client owns the shared slot. */
static int stream_client(void) {
    int fd=shm_open(SHM_NAME,O_RDWR,0);if(fd<0){perror("server not running");return 1;}
    Shared *sh=mmap(NULL,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    sem_t *req=sem_open(REQ_NAME,0),*done=sem_open(DONE_NAME,0),*lock=sem_open(LOCK_NAME,0);
    if(sh==MAP_FAILED||req==SEM_FAILED||done==SEM_FAILED||lock==SEM_FAILED){perror("shared IPC");return 1;}
    uint32_t shape[2];
    while(fread(shape,sizeof(uint32_t),2,stdin)==2) {
        int B=(int)shape[0],T=(int)shape[1];
        if(B<1||B>MAX_B||T<2||T>MAX_T){fprintf(stderr,"bad input shape\n");return 1;}
        while(sem_wait(lock)&&errno==EINTR){}
        size_t count=(size_t)B*F*T;
        if(fread(sh->input,sizeof(float),count,stdin)!=count){fprintf(stderr,"short input\n");sem_post(lock);return 1;}
        sh->batch=B;sh->time=T;sh->state=1;sem_post(req);
        while(sem_wait(done)&&errno==EINTR){}
        if(sh->error){fprintf(stderr,"server rejected shape\n");sem_post(lock);return 1;}
        count=(size_t)((T+1)/2)*B*C;
        if(fwrite(sh->output,sizeof(float),count,stdout)!=count){perror("stdout");sem_post(lock);return 1;}
        fflush(stdout);
        sem_post(lock);
    }
    munmap(sh,sizeof(Shared));close(fd);sem_close(req);sem_close(done);sem_close(lock);return 0;
}

/* In-process decode for tests: reads B*65*T float32 from INPUT, writes output. */
static int local_infer(const char *weights,const char *in,const char *out,int B,int T) {
    static Model m;if(load_model(&m,weights)){perror(weights);return 1;}
    if(B<1||B>MAX_B||T<2||T>MAX_T){fprintf(stderr,"bad input shape\n");return 1;}
    size_t n=(size_t)B*F*T,count=(size_t)((T+1)/2)*B*C;
    float *x=malloc(n*sizeof(float)),*y=malloc(count*sizeof(float));
    FILE *fp=fopen(in,"rb");if(!fp){perror(in);return 1;}
    if(fread(x,sizeof(float),n,fp)!=n){fprintf(stderr,"short input\n");return 1;}fclose(fp);
    infer(&m,x,y,B,T);
    fp=fopen(out,"wb");if(!fp){perror(out);return 1;}
    int ok=fwrite(y,sizeof(float),count,fp)==count;fclose(fp);free(x);free(y);return ok?0:1;
}

int main(int argc,char **argv) {
    if(argc==3&&!strcmp(argv[1],"serve"))return serve(argv[2]);
    if(argc==6&&!strcmp(argv[1],"infer"))return client(argv[2],argv[3],atoi(argv[4]),atoi(argv[5]));
    if(argc==7&&!strcmp(argv[1],"local"))return local_infer(argv[2],argv[3],argv[4],atoi(argv[5]),atoi(argv[6]));
    if(argc==2&&!strcmp(argv[1],"stream"))return stream_client();
    fprintf(stderr,"usage: %s serve WEIGHTS | infer INPUT OUTPUT B T | local WEIGHTS INPUT OUTPUT B T | stream\n",argv[0]);return 2;
}
#endif
