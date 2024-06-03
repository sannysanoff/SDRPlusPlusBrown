
#pragma once

// keep logs cliean
#define noisy_ft8 0

#include "../fftw_mshv_plug.h"

#include "wasm_defines.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <string>

#include <complex.h>
//#define complex		_Complex
#include <memory>
#include <cstring>
#include <thread>

#ifdef I
#undef I
#endif

#ifdef _WIN32
#include <stdlib.h>
#include <malloc.h>
#include <windows.h>

typedef std::thread pthread_t;
#define pthread_create(pThrVar, NU, fun, arg) std::swap(*pThrVar, std::thread([=] { fun(arg); }))
#define pthread_detach(thr)                   thr.detach();
#define pthread_exit(thr)                     TerminateThread(GetCurrentThread(), 0)
#else
#include <unistd.h>
#endif

#include <stdarg.h>

#include <utils/usleep.h>
#include <functional>


inline int toInt(const std::string &s, bool &ok, int radix) {
    int rv;
    if (s.size() == 0) {
        ok = false;
        return 0;
    }
    if (radix == 16) {
        ok = 1 == sscanf(s.c_str(), "%x", &rv);
    } else if (radix == 10) {
        ok = 1 == sscanf(s.c_str(), "%d", &rv);
    } else {
        abort();
    }
    return rv;
}


struct QString;

struct QStringList {
    std::shared_ptr<std::vector<std::string>> list;

    QStringList() {
        list = std::make_shared<std::vector<std::string>>();
    }

    QStringList &operator << (const QString &s);

    int count() {
        return (int)list->size();
    }

    void clear() {
        list->clear();
    }



    [[nodiscard]] QString operator[](int index) const;
    [[nodiscard]] QString at(int i) const;

    void replace(int i, QString qString);
};


struct QRegExp;

struct deferred {
    std::function<void()> f;
    deferred(std::function<void()> f) : f(f) {}
    ~deferred() {
        f();
    }
};


struct QChar {
    char c;

    QChar() {
        c = 0;
    }

    QChar(char c) {
        this->c = c;
    }

    operator char() const {
        return c;
    }

    bool isLetter() {
        return ::isalpha(c);
    }
    bool isDigit() {
        return ::isdigit(c);
    }
    char toLatin1() {
        return c;
    }
};

struct QString {
    std::shared_ptr<std::string> str;

    QString() {
        str = std::make_shared<std::string>();
    }

    QString(const QString &other);

    QString(QString &&other) noexcept ;

    QString & operator= (const QString &other);

    QString & operator= (const char *ptr);

    void verify();

    void initWithConstChar(const char *init, int len);

    QString(const char *init);
    QString(const char *init, int len);

    explicit QString(char init);

    QString(const std::string &init);

    int count() const {
        return str->length();
    }

    int length() const {
        return str->length();
    }

    QChar at(int index) const {
        return str->at(index);
    }
    bool contains(const QRegExp &);
    bool contains(const char* s) {
        return str->find(s) != std::string::npos;
    }

    auto begin() {
        return str->begin();
    }
    auto end() {
        return str->end();
    }

    int indexOf(const QString &x, int start = 0) const {
        auto rv = str->find(*x.str, start);
        if (rv == std::string::npos) {
            return -1;
        }
        return rv;
    }

    int indexOf(char x, int start = 0) const {
        auto rv = str->find(x, start);
        if (rv == std::string::npos) {
            return -1;
        }
        return rv;
    }

    int indexOf(const char *x, int start = 0) const {
        auto rv = str->find(x, start);
        if (rv == std::string::npos) {
            return -1;
        }
        return rv;
    }

    QString operator+(const char *s);

    QString operator+(const char s);

    [[nodiscard]] QString arg(int v) const {
        if (!this->str) {
            abort();
        }
        if (*this->str == "%1") {
            char buf[100];
            snprintf(buf, sizeof buf, "%d", v);
            return QString(buf);
        }
        abort(); //
        return *this;
    }

    [[nodiscard]] QString arg(int val, int width, int base, QChar fill= ' ') const {
        char buf[10];
        char buf2[100];
        const char *pred;
        if (fill == ' ') {
            pred = "";
        }
        if (fill == '0') {
            pred = "0";
        }

        if (base == 10) {
            snprintf(buf, sizeof buf, "%%%s%dd", pred, width);
        } else if (base == 16) {
            snprintf(buf, sizeof buf, "%%%s%dx", pred, width);
        } else {
            abort();
        }
        snprintf(buf2, sizeof(buf2), buf, val);
        return std::string(buf2);
    }

    [[nodiscard]] QString arg(double v, int width, char fmt, int prec, QChar fill = ' ') const {
        char buf[10];
        char buf2[100];
        snprintf(buf, sizeof buf, "%%%d.%d%c", width, prec, fmt);
        snprintf(buf2, sizeof buf2, buf, v);
        return std::string(buf2);
    }

    void remove(const char *x);

    bool operator ==(const char *x) const;

    bool operator >=(const char *x) const;

    bool operator <=(const char *x) const;

    bool operator ==(const QString &x) const;

    bool operator !=(const QString &x) const;

    bool operator !=(const char *x) const;

    QString operator +(const QString &other) const {
        return *str + *other.str;
    }

    [[nodiscard]] QString rightJustified(int w, QChar filler) const {
        unsigned long myLen = str->length();
        if (myLen >= w) {
            return *this;
        }
        std::string rv(w - myLen, filler.c);
        rv.append(*str);
        return rv;
        
    }

    [[nodiscard]] QString leftJustified(int w, QChar filler) const {
        unsigned long myLen = str->length();
        if (myLen >= w) {
            return *this;
        }
        std::string padding(w - myLen, filler.c);
        return *str + padding;
    }

    [[nodiscard]] QString trimmed() const {
        if (str->empty()) {
            return *this;
        }
        if (isspace((*str)[0]) || isspace((*str)[str->length() - 1])) {
            auto ns = *str;
            while(!ns.empty() && std::isspace(ns[0])) {
                ns = ns.substr(1);
            }
            while(!ns.empty() && std::isspace(ns[ns.length()-1])) {
                ns.resize(ns.length()-1);
            }
            return ns;
        } else {
            return *this;
        }
    }
    [[nodiscard]] QString toUpper() const {
        auto nv = *str;
        for(auto &q : nv) {
            q = (char)std::toupper(q);
        }
        return nv;
    }

    void mutated();

    void append(const char *s);

    void append(const QString &s);

    int lastIndexOf(const QString &s, int trimFirst) const {
        return this->mid(0, trimFirst).lastIndexOf(s);
    }

    void insert(int pos, const QString &s);

    bool isEmpty() const {
        return str->empty();
    }

    int lastIndexOf(const QString &s) const {
        auto rv = str->rfind(*s.str);
        if (rv == std::string::npos) {
            return -1;
        }
        return rv;
    }

    void replaceWith(const std::string &s);

    void prepend(const QString &s);

    void prepend(const char *s);

    void append(char s);

    void prepend(char s);

    QChar &operator[](int index) const {
        if (index >= str->length()) {
            fprintf(stderr, "string index[] error, index=%d length=%zu  str=%s\n", index, str->length(), str->c_str());
            abort();
        }
        QChar *p = (QChar *)str->data()+index;
        return *p;
    }

    int toInt(bool *ok = nullptr, int radix = 10) const {
        bool ok0;
        if (!ok) {
            ok = &ok0;
        }
        return ::toInt(*str, *ok, radix);
    }


    [[nodiscard]] QString mid(int i, int len) const {
        if (i+len > str->length()) {
            len = str->length()-i;
        }
        if (len < 0) {
            len = 0;
        }
        if (len == 0) {
            return QString("");
        }
        return str->substr(i, len);
    }

    [[nodiscard]] QString midRef(int i, int len) const {
        return this->mid(i, len);
    }

    QStringList split(const char* separ) const;

    void replace(int start, int len, QString ns);

    void replace(const char* string, const char* string1);

    bool startsWith(const char* string) {
        return str->find(string) == 0;
    }
};

struct QRegExp {
    QRegExp(const char *init) {
        abort();
    }
    static const int CaseInsensitive = 0;
    int CaseSensitive = 1;
    int caseSensitivity = 1;
    void setCaseSensitivity(int i) {    // 1 = sensitive, 0 = insensitive
        this->caseSensitivity = CaseInsensitive;
    }

    bool exactMatch(const QString &s) {
        abort();
    }
};


inline QString operator + (const char *a, const QString &s) {
    return std::string(a) + *s.str;
}

inline std::ostream &operator << (std::ostream &os, const QString &s) {
    os << *s.str;
    return os;
}

struct fftw_complexW {
    plug_complex_float c;
    constexpr fftw_complexW() : c() {
        c[0] = 0;
        c[1] = 0;
    }
};

constexpr fftw_complexW mk_fftw_complex(double re, double im) {
    fftw_complexW w;
    w.c[0] = re;
    w.c[1] = im;
    return w;
}

constexpr auto mk_complex(double re, double im) {
    return std::complex<double>(re, im);
}

constexpr auto complex_zero = mk_complex(0, 0);
constexpr auto complex_i = mk_complex(0, 1);

inline double creal(std::complex<double> c) {
    return c.real();
}

inline double cimag(std::complex<double> c) {
    return c.imag();
}

inline double cabs(std::complex<double> c) {
    return abs(c);
}

void mshv_init();

#define DYNAMIC_ARRAY(typ, name, size) \
    std::vector<typ> name##0(size); \
    auto name = (name##0).data();


extern std::function<void(const char *line)> decodeResultOutputFun;
#ifdef __wasm__
WASM_IMPORT("decodeResultOutput") void decodeResultOutput(const char *line);
#else
inline void decodeResultOutput(const char *line) { if (noisy_ft8) printf("%s\n", line); if (decodeResultOutputFun) decodeResultOutputFun(line); }
#endif

void debugPrintf(const char *fmt, ...);

std::string arrayToString(const char *name, const float *arr, int len);
std::string arrayToStringD(const char *name, const double *arr, int len);
std::string arrayToStringC(const char *name, const plug_complex_float *arr, int len);
std::string arrayToStringSCD(const char *name, const std::complex<double> *arr, int len);
void printArray(const char *name, const float *arr, int len);
void printArrayC(const char *name, const plug_complex_float *arr, int len);
void printArrayD(const char *name, const double *arr, int len);
void printArraySCD(const char *name, const std::complex<double> *arr, int len);
