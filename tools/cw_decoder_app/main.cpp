// CW Decoder v17 - STFT N=8192 H=2048 (75% overlap) + energy envelope + HamFist beam decoder
#include "cw_decoder_core.h"
#include "cw_classifier.h"
#include "morse_data.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <chrono>
#include <fftw3.h>

struct Config {
    double center = 14074662.0;
    float  fs = 192000.0f;
    float  thresh_mult = 5.0f;
    float  hys = 0.7f;
};

struct WavReader {
    FILE* fp=0; uint16_t ch=0; uint32_t sr=0; uint16_t bps=0;
    size_t open(const char* p) {
        fp=fopen(p,"rb");if(!fp)return 0;
        char h[44];fread(h,1,44,fp);uint32_t ds=0,pos=12;
        while(pos+8<44){uint32_t sz=*(uint32_t*)(h+pos+4);if(!memcmp(h+pos,"fmt ",4)){ch=*(uint16_t*)(h+pos+10);sr=*(uint32_t*)(h+pos+12);bps=*(uint16_t*)(h+pos+22);}pos+=8+sz;}
        fseek(fp,12,SEEK_SET);char b[8];while(fread(b,1,8,fp)==8){uint32_t sz=*(uint32_t*)(b+4);if(!memcmp(b,"data",4)){ds=sz;break;}fseek(fp,sz,SEEK_CUR);}
        return ds/(ch*(bps/8));
    }
    size_t read(float* I,float* Q,size_t n){if(!fp)return 0;
        size_t bpf=ch*(bps/8);std::vector<uint8_t> r(n*bpf);
        size_t rd=fread(r.data(),1,n*bpf,fp);size_t f=rd/bpf;
        if(bps==16){int16_t* s=(int16_t*)r.data();for(size_t i=0;i<f;i++){I[i]=s[i*ch]/32768.0f;Q[i]=s[i*ch+1]/32768.0f;}}
        return f;
    }
    void close(){if(fp){fclose(fp);fp=0;}} ~WavReader(){close();}
};

struct Channel {
    uint32_t id; float freq_rel; int bin_idx;
    std::unique_ptr<c_cw_decoder> dec;
    std::vector<s_observation> obs;
    float nf=0.001f;
    float snr=0;
    bool key=0; float dur=0;
    std::string txt;
    
    Channel(uint32_t i, float f, int b) : id(i), freq_rel(f), bin_idx(b), 
        dec(std::make_unique<c_cw_decoder>(i)) {}
    
    std::vector<s_observation> clean_obs() {
        std::vector<s_observation> out;
        for(auto& o : obs) {
            if(o.duration < 15.0f) continue;
            if(!o.mark && o.duration > 500.0f) continue;
            out.push_back(o);
        }
        return out;
    }
    void do_decode(float fm) {
        auto co = clean_obs(); if(co.size() < 8) return;
        std::vector<s_observation> co_vec = co;
        dec->decode(co_vec.data(), (int)co_vec.size());
        std::string t = dec->get_text();
        if(!t.empty()){
            txt += t + " ";
        }
    }
};

inline float b2f(int i,int N,float bw){return(float)(i-N/2)*bw;}
static float bh(int i,int N){
    float a0=0.35875f,a1=0.48829f,a2=0.14128f,a3=0.01168f;
    return a0-a1*cosf(2*M_PI*i/(N-1))+a2*cosf(4*M_PI*i/(N-1))-a3*cosf(6*M_PI*i/(N-1));
}

int main(int argc,char* argv[]){
    const char* path=argc>1?argv[1]:"/opt/data/baseband_14074662Hz_12-04-14_17-02-2024-cw-contest.wav";
    Config cfg;
    WavReader wav; size_t total=wav.open(path); if(!total) return 1;
    cfg.fs=(float)wav.sr;
    
    int N=8192; float bw=cfg.fs/N; int H=2048;
    float frame_ms = 1000.0f*H/cfg.fs;

    printf("[CFG] N=%d H=%d frame=%.2fms bw=%.1fHz overlap=%d%% BH\n",
           N,H,frame_ms,bw,(int)(100*(N-H)/N));
    
    fftw_complex* fi=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*N);
    fftw_complex* fo=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*N);
    if(!fi||!fo) return 1;
    fftw_plan plan=fftw_plan_dft_1d(N,fi,fo,FFTW_FORWARD,FFTW_ESTIMATE);
    if(!plan) return 1;
    
    std::vector<float> win(N), nf(N,0), mag(N);
    for(int i=0;i<N;i++) win[i]=bh(i,N);
    
    size_t total_fr = total/H;
    
    // Pass 1: detect channels
    std::vector<int> peak_count(N,0); std::vector<float> peak_accum(N,0);
    std::vector<float> I(N), Q(N);
    for(size_t f=0;f<500&&f<total_fr;f++){
        size_t rem=N-H; 
        if(f==0) wav.read(I.data(),Q.data(),N);
        else{
            memmove(I.data(),I.data()+H,rem*sizeof(float));
            memmove(Q.data(),Q.data()+H,rem*sizeof(float));
            if(wav.read(I.data()+rem,Q.data()+rem,H)<H) break;
        }
        for(int i=0;i<N;i++){fi[i][0]=I[i]*win[i];fi[i][1]=Q[i]*win[i];}
        fftw_execute(plan);
        for(int i=0;i<N;i++){int s=(i+N/2)%N;mag[i]=fo[s][0]*fo[s][0]+fo[s][1]*fo[s][1];}
        for(int i=0;i<N;i++){if(nf[i]==0)nf[i]=mag[i];else if(mag[i]<nf[i]*2.0f)nf[i]=0.99f*nf[i]+0.01f*mag[i];}
        int b0=std::max(0,N/2+(int)((14000000-cfg.center)/bw));
        int b1=std::min(N-1,N/2+(int)((14070000-cfg.center)/bw));
        for(int i=b0+2;i<=b1-2;i++){
            if(mag[i]>nf[i]*cfg.thresh_mult && mag[i]>=mag[i-1] && mag[i]>=mag[i+1])
            {peak_accum[i]+=mag[i];peak_count[i]++;}
        }
    }
    std::vector<std::pair<int,float>> cand;
    int b0=std::max(0,N/2+(int)((14000000-cfg.center)/bw));
    int b1=std::min(N-1,N/2+(int)((14070000-cfg.center)/bw));
    for(int i=b0;i<=b1;i++){if(peak_count[i]>3)cand.push_back({i,peak_accum[i]/peak_count[i]});}
    std::sort(cand.begin(),cand.end(),[](auto& a,auto& b){return a.second>b.second;});
    
    std::vector<Channel> channels; uint32_t nid=1;
    for(auto& c:cand){
        float fr=b2f(c.first,N,bw); bool dup=false;
        for(auto& ch:channels){if(fabs(ch.freq_rel-fr)<150){dup=true;break;}}
        if(!dup) channels.emplace_back(nid++,fr,c.first);
    }
    if(channels.size()>10) channels.erase(channels.begin()+10,channels.end());
    printf("[CH] %zu channels from %zu candidates\n",channels.size(),cand.size());
    for(auto& ch: channels) printf("  Ch%u: %.1f kHz (bin %d)\n", ch.id, (cfg.center+ch.freq_rel)/1000, ch.bin_idx);
    
    // Pass 2: full processing
    wav.close(); wav.open(path);
    memset(I.data(),0,N*sizeof(float)); memset(Q.data(),0,N*sizeof(float));
    auto t1=std::chrono::steady_clock::now();
    
    for(size_t f=0;f<total_fr;f++){
        size_t rem=N-H;
        if(f==0) wav.read(I.data(),Q.data(),N);
        else{
            memmove(I.data(),I.data()+H,rem*sizeof(float));
            memmove(Q.data(),Q.data()+H,rem*sizeof(float));
            if(wav.read(I.data()+rem,Q.data()+rem,H)<H) break;
        }
        for(int i=0;i<N;i++){fi[i][0]=I[i]*win[i];fi[i][1]=Q[i]*win[i];}
        fftw_execute(plan);
        for(int i=0;i<N;i++){int s=(i+N/2)%N;mag[i]=fo[s][0]*fo[s][0]+fo[s][1]*fo[s][1];}
        for(int i=0;i<N;i++){if(mag[i]<nf[i]*2.0f)nf[i]=0.99f*nf[i]+0.01f*mag[i];nf[i]=std::max(nf[i],1e-12f);}
        
        for(auto& ch: channels){
            float ce = mag[ch.bin_idx];
            ch.nf += 0.005f * (ce - ch.nf);
            if(ce < ch.nf) ch.nf = 0.9f*ch.nf + 0.1f*ce;
            ch.nf = std::max(ch.nf, 1e-12f);
            
            float thr = ch.nf * cfg.thresh_mult;
            bool kd = ch.key ? (ce > thr*cfg.hys) : (ce > thr);
            if(kd) ch.snr=0.99f*ch.snr+0.01f*(ce/ch.nf);
            else   ch.snr=0.999f*ch.snr+0.001f*(ce/ch.nf);
            
            if(kd!=ch.key){
                s_observation o; o.mark=ch.key?1:0; o.duration=ch.dur*frame_ms;
                ch.obs.push_back(o);
                ch.key=kd; ch.dur=0;
            }
            ch.dur++;
            
            if(ch.obs.size()>=25){
                ch.do_decode(frame_ms);
                ch.obs.clear(); ch.dur=0;
            }
            if(ch.dur>=300 && ch.obs.size()>=5){
                ch.do_decode(frame_ms);
                ch.obs.clear(); ch.dur=0; ch.dec->reset(); ch.snr=0;
            }
        }
        if(f%5000==0&&f>0) fprintf(stderr, "\rPROC: %.0f%%",100.0f*f/total_fr);
    }
    if(total_fr>5000) fprintf(stderr, "\rPROC: 100%%\n");
    for(auto& ch:channels) if(!ch.obs.empty()) ch.do_decode(frame_ms);
    
    float el=std::chrono::duration<float>(std::chrono::steady_clock::now()-t1).count();
    printf("\n=== DECODED TEXT (%.1fs) ===\n",el);
    int tc=0;
    for(auto& ch:channels){
        printf("  Ch%u [%.3f MHz]: %s\n", ch.id, (cfg.center+ch.freq_rel)/1e6, ch.txt.empty()?"(none)":ch.txt.c_str());
        if(!ch.txt.empty()) tc++;
    }
    printf("Channels with decodes: %d/%zu\n",tc,channels.size());
    fftw_destroy_plan(plan);fftw_free(fi);fftw_free(fo);
    return 0;
}
