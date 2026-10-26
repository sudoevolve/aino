// =============================================================================
//  aino 示例 —— 函数显示器
//
//  左侧选公式，右侧实时画图；参数 a/b/c 用滑杆拖，图跟着变。
//  滚轮缩放、拖拽平移、双击复位、空格暂停动画。
//
//  六类公式走四条不同的绘图路径：
//    1. 显函数 y = f(x)       按屏幕像素密度采样，连成折线
//    2. 参数方程 / 极坐标      均匀采样参数 u，映射到平面后连折线
//    3. 隐函数 F(x, y) = 0    marching squares（等值线追踪）
//    4. 标量场 z = f(x, y)    热力图（颜色游程合并）+ 等高线
//  第六类"自定义"支持手打公式 —— 文件里带一个表达式编译器（→ 字节码 → 求值）。
//
//  这个文件里没有任何绘图库：曲线就是 ui.draw_line() 画的一堆短线段，
//  热力图就是 ui.fill_rect() 画的色块。aino 只提供笔画，几何全在这里算。
//
//  编译：
//    g++ -std=c++17 -O2 -static -s function_viewer.cpp -o function_viewer.exe \
//        -mwindows -luser32 -lgdi32
//  表达式自测（不开窗口，结果写文件）：
//    AINO_FN_SELFTEST=out.txt ./function_viewer.exe
//
//  作者：sudoevolve <sudoevolve@gmail.com>
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Style;

namespace {

constexpr float kPi  = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

// =============================================================================
//  一、表达式编译器
//
//  语法（和大多数计算器 / 绘图工具一致）：
//      expr   := term (('+'|'-') term)*
//      term   := unary (('*'|'/'|'%') unary | power)*   // 省略乘号：2x / 3(x+1) / (x+1)(x-1)
//      unary  := ('-'|'+') unary | power
//      power  := primary ('^' unary)?          // 右结合，所以 -x^2 = -(x^2)
//      primary:= 数字 | 变量 | 函数(参数, ...) | '(' expr ')' | '|' expr '|'
//  变量：x y t a b c（极坐标里 x 就是 θ，参数方程里 x 就是 u，都可以直接写 u / theta / th）
//  常量：pi tau e phi
//  隐函数可以直接写等式：x^2 + y^2 = 1（内部当 x^2+y^2-1 = 0 处理）
//
//  编译成字节码而不是遍历语法树：隐函数一帧要算一万多个点，
//  递归下降求值那点开销会直接吃掉帧预算。
// =============================================================================
namespace fx {

enum Op : int {
    PUSH_C, VAR_X, VAR_Y, VAR_T, VAR_A, VAR_B, VAR_C,
    ADD, SUB, MUL, DIV, POW, NEG,
    F_SIN, F_COS, F_TAN, F_SEC, F_CSC, F_COT,
    F_ASIN, F_ACOS, F_ATAN, F_ATAN2,
    F_SINH, F_COSH, F_TANH, F_ASINH, F_ACOSH, F_ATANH,
    F_EXP, F_LOG, F_LOG10, F_LOG2, F_SQRT, F_CBRT, F_ABS,
    F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_FRAC, F_SIGN,
    F_MIN, F_MAX, F_POW2, F_HYPOT, F_MOD,
    F_CLAMP, F_LERP, F_STEP, F_SMOOTH
};

struct Instr { int op; float imm; };

struct Program {
    std::vector<Instr> code;
    bool        ok = false;
    std::string error = "空表达式";
};

// 每个指令的栈效应（编译后校验栈深度，避免求值时越界）
inline void stack_effect(int op, int& push, int& pop) {
    push = 1; pop = 0;
    switch (op) {
    case PUSH_C: case VAR_X: case VAR_Y: case VAR_T:
    case VAR_A: case VAR_B: case VAR_C: break;
    case F_CLAMP: case F_LERP: case F_SMOOTH:
        push = 1; pop = 3; break;                 // 三元
    case ADD: case SUB: case MUL: case DIV: case POW:
    case F_MIN: case F_MAX: case F_POW2: case F_HYPOT: case F_MOD:
    case F_ATAN2: case F_STEP:
        push = 1; pop = 2; break;                 // 二元
    default: /* 一元函数 / 取负 */ push = 1; pop = 1; break;
    }
}

struct Parser {
    const std::string& src;
    size_t  i = 0;
    Program& out;
    std::string err;
    int abs_depth = 0;          // |x| 的嵌套层数（见 starts_factor）

    explicit Parser(const std::string& s, Program& p) : src(s), out(p) {}

    void skip_ws() {
        while (i < src.size() && static_cast<unsigned char>(src[i]) <= ' ') ++i;
    }
    bool eof() { skip_ws(); return i >= src.size(); }
    bool eat(char c) {
        skip_ws();
        if (i < src.size() && src[i] == c) { ++i; return true; }
        return false;
    }
    void emit(int op, float imm = 0.0f) { out.code.push_back({op, imm}); }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    bool parse() {
        if (!expr()) return false;
        skip_ws();
        if (i < src.size()) return fail("多余的字符");
        if (out.code.empty()) return fail("空表达式");
        return true;
    }

    bool expr() {
        if (!term()) return false;
        for (;;) {
            if (eat('+'))      { if (!term()) return false; emit(ADD); }
            else if (eat('-')) { if (!term()) return false; emit(SUB); }
            else return true;
        }
    }
    bool term() {
        if (!unary()) return false;
        for (;;) {
            if (eat('*'))      { if (!unary()) return false; emit(MUL); }
            else if (eat('/')) { if (!unary()) return false; emit(DIV); }
            else if (eat('%')) { if (!unary()) return false; emit(F_MOD); }
            else if (starts_factor()) {          // 2x / 3(x+1) / (x+1)(x-1) —— 省略乘号
                if (!power()) return false;
                emit(MUL);
            }
            else return true;
        }
    }
    // 下一个记号能不能直接当因子（省掉乘号）。数字开头不算：
    // "1 2" 是写错了，不该被悄悄当成 1*2。
    // 竖线只在"不在绝对值里"时才算开头，否则 |x| 的右竖线会被当成新的 |。
    bool starts_factor() {
        skip_ws();
        if (i >= src.size()) return false;
        const char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return true;
        if (c == '(') return true;
        if (c == '|') return abs_depth == 0;
        return false;
    }
    bool unary() {
        if (eat('-')) { if (!unary()) return false; emit(NEG); return true; }
        if (eat('+')) return unary();
        return power();
    }
    bool power() {
        if (!primary()) return false;
        if (eat('^')) {
            if (!unary()) return false;   // 右结合；也允许 2^-1
            emit(POW);
        }
        return true;
    }

    struct FnDef { const char* name; int op; int argc; };

    bool primary() {
        skip_ws();
        if (i >= src.size()) return fail("表达式不完整");
        const char c = src[i];

        if (c == '(') {
            ++i;
            if (!expr()) return false;
            if (!eat(')')) return fail("缺少右括号 )");
            return true;
        }
        if (c == '|') {                       // |x| —— 绝对值
            ++i; ++abs_depth;
            if (!expr()) return false;
            if (!eat('|')) { --abs_depth; return fail("绝对值缺少收尾的 |"); }
            --abs_depth;
            emit(F_ABS);
            return true;
        }
        if ((c >= '0' && c <= '9') || c == '.') {
            char* end = nullptr;
            const float v = std::strtof(src.c_str() + i, &end);
            if (end == src.c_str() + i) return fail("数字写错了");
            i = static_cast<size_t>(end - src.c_str());
            emit(PUSH_C, v);
            return true;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            const size_t s = i;
            while (i < src.size() &&
                   ((src[i] >= 'a' && src[i] <= 'z') || (src[i] >= 'A' && src[i] <= 'Z') ||
                    (src[i] >= '0' && src[i] <= '9') || src[i] == '_')) ++i;
            std::string name = src.substr(s, i - s);
            for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (name == "x" || name == "u" || name == "theta" || name == "th") { emit(VAR_X); return true; }
            if (name == "y") { emit(VAR_Y); return true; }
            if (name == "t") { emit(VAR_T); return true; }
            if (name == "a") { emit(VAR_A); return true; }
            if (name == "b") { emit(VAR_B); return true; }
            if (name == "c") { emit(VAR_C); return true; }
            if (name == "pi") { emit(PUSH_C, kPi); return true; }
            if (name == "tau") { emit(PUSH_C, kTau); return true; }
            if (name == "e")  { emit(PUSH_C, 2.7182818284590452f); return true; }
            if (name == "phi") { emit(PUSH_C, 1.6180339887498948f); return true; }

            static const FnDef kFns[] = {
                {"sin", F_SIN, 1}, {"cos", F_COS, 1}, {"tan", F_TAN, 1},
                {"sec", F_SEC, 1}, {"csc", F_CSC, 1}, {"cot", F_COT, 1},
                {"asin", F_ASIN, 1}, {"acos", F_ACOS, 1}, {"atan", F_ATAN, 1},
                {"atan2", F_ATAN2, 2},
                {"sinh", F_SINH, 1}, {"cosh", F_COSH, 1}, {"tanh", F_TANH, 1},
                {"asinh", F_ASINH, 1}, {"acosh", F_ACOSH, 1}, {"atanh", F_ATANH, 1},
                {"exp", F_EXP, 1}, {"log", F_LOG, 1}, {"ln", F_LOG, 1},
                {"log10", F_LOG10, 1}, {"lg", F_LOG10, 1}, {"log2", F_LOG2, 1},
                {"sqrt", F_SQRT, 1}, {"cbrt", F_CBRT, 1}, {"abs", F_ABS, 1},
                {"floor", F_FLOOR, 1}, {"ceil", F_CEIL, 1}, {"round", F_ROUND, 1},
                {"trunc", F_TRUNC, 1}, {"frac", F_FRAC, 1},
                {"sign", F_SIGN, 1}, {"sgn", F_SIGN, 1},
                {"min", F_MIN, 2}, {"max", F_MAX, 2}, {"pow", F_POW2, 2},
                {"hypot", F_HYPOT, 2}, {"mod", F_MOD, 2},
                {"step", F_STEP, 2},
                {"clamp", F_CLAMP, 3}, {"lerp", F_LERP, 3}, {"mix", F_LERP, 3},
                {"smoothstep", F_SMOOTH, 3},
            };
            const FnDef* def = nullptr;
            for (const FnDef& f : kFns) if (name == f.name) { def = &f; break; }
            if (!def) return fail(("未知符号: " + name).c_str());

            if (!eat('(')) return fail(("函数要带括号: " + name).c_str());
            int argc = 0;
            if (!eat(')')) {
                for (;;) {
                    if (!expr()) return false;
                    ++argc;
                    if (eat(',')) continue;
                    if (eat(')')) break;
                    return fail("参数列表要闭合");
                }
            }
            if (argc != def->argc)
                return fail(("参数个数不对: " + name).c_str());
            emit(def->op);
            return true;
        }
        return fail("无法识别的字符");
    }
};

inline bool compile(const std::string& src, Program& out) {
    out.code.clear();
    out.ok = false;
    out.error.clear();
    if (src.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.error = "空表达式";
        return false;
    }
    // 隐函数习惯写法 F(x,y) = G(x,y)：等价于画 F-G = 0。
    // 只在括号外层的第一个 = 处切（== 之类没有比较运算符，不用管）。
    {
        int depth = 0;
        size_t eq = std::string::npos;
        for (size_t k = 0; k < src.size(); ++k) {
            if (src[k] == '(') ++depth;
            else if (src[k] == ')') --depth;
            else if (src[k] == '=' && depth == 0) {
                if (eq != std::string::npos) { out.error = "等号只能写一个"; return false; }
                eq = k;
            }
        }
        if (eq != std::string::npos) {
            const std::string both = "(" + src.substr(0, eq) + ")-(" + src.substr(eq + 1) + ")";
            return compile(both, out);
        }
    }
    Parser p(src, out);
    if (!p.parse()) {
        out.error = p.err.empty() ? "语法错误" : p.err;
        out.code.clear();
        return false;
    }
    // 栈深度校验：不够深的话求值时会把栈写穿
    int depth = 0, max_depth = 0;
    for (const Instr& in : out.code) {
        int push = 0, pop = 0;
        stack_effect(in.op, push, pop);
        depth -= pop;
        depth += push;
        max_depth = std::max(max_depth, depth);
    }
    if (max_depth > 40 || depth != 1) {
        out.error = "表达式太复杂";
        out.code.clear();
        return false;
    }
    out.ok = true;
    return true;
}

// 一帧算上万次，所以这里没有任何分支之外的抽象：线程栈 + 线性扫描。
inline float eval(const Program& p, float x, float y, float t, float a, float b, float c) {
    float st[48];
    int sp = 0;
    for (const Instr& in : p.code) {
        switch (in.op) {
        case PUSH_C: st[sp++] = in.imm; break;
        case VAR_X:  st[sp++] = x; break;
        case VAR_Y:  st[sp++] = y; break;
        case VAR_T:  st[sp++] = t; break;
        case VAR_A:  st[sp++] = a; break;
        case VAR_B:  st[sp++] = b; break;
        case VAR_C:  st[sp++] = c; break;
        case ADD: st[sp - 2] += st[sp - 1]; --sp; break;
        case SUB: st[sp - 2] -= st[sp - 1]; --sp; break;
        case MUL: st[sp - 2] *= st[sp - 1]; --sp; break;
        case DIV: st[sp - 2] /= st[sp - 1]; --sp; break;
        case POW: st[sp - 2] = std::pow(st[sp - 2], st[sp - 1]); --sp; break;
        case NEG: st[sp - 1] = -st[sp - 1]; break;
        case F_SIN:  st[sp - 1] = std::sin(st[sp - 1]); break;
        case F_COS:  st[sp - 1] = std::cos(st[sp - 1]); break;
        case F_TAN:  st[sp - 1] = std::tan(st[sp - 1]); break;
        case F_SEC:  st[sp - 1] = 1.0f / std::cos(st[sp - 1]); break;
        case F_CSC:  st[sp - 1] = 1.0f / std::sin(st[sp - 1]); break;
        case F_COT:  st[sp - 1] = 1.0f / std::tan(st[sp - 1]); break;
        case F_ASIN: st[sp - 1] = std::asin(st[sp - 1]); break;
        case F_ACOS: st[sp - 1] = std::acos(st[sp - 1]); break;
        case F_ATAN: st[sp - 1] = std::atan(st[sp - 1]); break;
        case F_ATAN2: st[sp - 2] = std::atan2(st[sp - 2], st[sp - 1]); --sp; break;
        case F_SINH: st[sp - 1] = std::sinh(st[sp - 1]); break;
        case F_COSH: st[sp - 1] = std::cosh(st[sp - 1]); break;
        case F_TANH: st[sp - 1] = std::tanh(st[sp - 1]); break;
        case F_ASINH: st[sp - 1] = std::asinh(st[sp - 1]); break;
        case F_ACOSH: st[sp - 1] = std::acosh(st[sp - 1]); break;
        case F_ATANH: st[sp - 1] = std::atanh(st[sp - 1]); break;
        case F_EXP:  st[sp - 1] = std::exp(st[sp - 1]); break;
        case F_LOG:  st[sp - 1] = std::log(st[sp - 1]); break;
        case F_LOG10: st[sp - 1] = std::log10(st[sp - 1]); break;
        case F_LOG2: st[sp - 1] = std::log2(st[sp - 1]); break;
        case F_SQRT: st[sp - 1] = std::sqrt(st[sp - 1]); break;
        case F_CBRT: st[sp - 1] = std::cbrt(st[sp - 1]); break;
        case F_ABS:  st[sp - 1] = std::fabs(st[sp - 1]); break;
        case F_FLOOR: st[sp - 1] = std::floor(st[sp - 1]); break;
        case F_CEIL:  st[sp - 1] = std::ceil(st[sp - 1]); break;
        case F_ROUND: st[sp - 1] = std::round(st[sp - 1]); break;
        case F_TRUNC: st[sp - 1] = std::trunc(st[sp - 1]); break;
        case F_FRAC:  st[sp - 1] -= std::floor(st[sp - 1]); break;
        case F_SIGN:  st[sp - 1] = float((st[sp - 1] > 0) - (st[sp - 1] < 0)); break;
        case F_MIN: st[sp - 2] = std::min(st[sp - 2], st[sp - 1]); --sp; break;
        case F_MAX: st[sp - 2] = std::max(st[sp - 2], st[sp - 1]); --sp; break;
        case F_POW2: st[sp - 2] = std::pow(st[sp - 2], st[sp - 1]); --sp; break;
        case F_HYPOT: st[sp - 2] = std::hypot(st[sp - 2], st[sp - 1]); --sp; break;
        case F_MOD: st[sp - 2] = std::fmod(st[sp - 2], st[sp - 1]); --sp; break;
        case F_STEP: st[sp - 2] = (st[sp - 1] < st[sp - 2]) ? 0.0f : 1.0f; --sp; break;
        case F_CLAMP: {
            const float hi = st[sp - 1], lo = st[sp - 2], v = st[sp - 3];
            st[sp - 3] = v < lo ? lo : (v > hi ? hi : v);
            sp -= 2;
            break;
        }
        case F_LERP: {
            const float u = st[sp - 1], b = st[sp - 2], a = st[sp - 3];
            st[sp - 3] = a + (b - a) * u;
            sp -= 2;
            break;
        }
        case F_SMOOTH: {   // smoothstep(e0, e1, x)
            const float x = st[sp - 1], e1 = st[sp - 2], e0 = st[sp - 3];
            float u = (e1 == e0) ? 0.0f : (x - e0) / (e1 - e0);
            u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            st[sp - 3] = u * u * (3.0f - 2.0f * u);
            sp -= 2;
            break;
        }
        default: break;
        }
    }
    return sp > 0 ? st[sp - 1] : 0.0f;
}

} // namespace fx

// =============================================================================
//  二、公式表
// =============================================================================

enum class Kind { Curve, Param, Polar, Implicit, Field };

const char* kind_note(Kind k) {
    switch (k) {
    case Kind::Curve:    return "显函数 y = f(x)";
    case Kind::Param:    return "参数方程";
    case Kind::Polar:    return "极坐标 r = f(θ)";
    case Kind::Implicit: return "隐函数 F(x, y) = 0";
    case Kind::Field:    return "标量场 z = f(x, y)";
    }
    return "";
}

struct Knob { std::string label; float lo, hi, def; };

using CurveFn = float (*)(float, const float*, float);
using ParamFn = void  (*)(float, const float*, float, float&, float&);
using FieldFn = float (*)(float, float, const float*, float);

struct Preset {
    std::string name;
    std::string formula;
    Kind  kind = Kind::Curve;
    float half_h  = 2.0f;   // 默认纵向半视野（世界单位）
    float stretch = 0.0f;   // 0 = 等比例（half_w = half_h × 视口宽高比）；否则 half_w = half_h × stretch
    float u0 = 0.0f, u1 = kTau;   // 参数方程的采样区间
    std::vector<Knob> knobs;
    CurveFn curve = nullptr;
    ParamFn param = nullptr;
    FieldFn field = nullptr;
};

// ---- 显函数 ----
float f_sine(float x, const float* p, float t)  { return std::sin(p[0] * x - t); }
float f_damped(float x, const float* p, float t) {
    return std::exp(-p[1] * x * x) * std::sin(p[0] * x - t);
}
float f_gauss(float x, const float* p, float)   { return p[0] * std::exp(-x * x / p[1]); }
float f_tanh(float x, const float* p, float)    { return p[0] * std::tanh(p[1] * x); }
float f_sinc(float x, const float* p, float) {
    const float u = p[0] * x;
    return std::fabs(u) < 1e-6f ? 1.0f : std::sin(u) / u;
}
float f_cubic(float x, const float* p, float)   { return x * x * x * 0.125f - p[0] * x; }
float pulse(float u, float c, float w) {
    const float d = (u - c) / w;
    return std::exp(-d * d);
}
float f_ecg(float x, const float* p, float t) {
    // 一段合成的 P-QRS-T：几个错开的高斯峰，够像心电图了
    float u = std::fmod(x - t * 0.9f, kTau);
    if (u < 0.0f) u += kTau;
    const float v = 0.10f * pulse(u, 0.90f, 0.22f)   // P
                  - 0.16f * pulse(u, 1.56f, 0.09f)   // Q
                  + 1.00f * pulse(u, 1.74f, 0.075f)  // R
                  - 0.30f * pulse(u, 1.94f, 0.095f)  // S
                  + 0.24f * pulse(u, 2.75f, 0.30f);  // T
    return p[0] * v;
}

// ---- 参数方程 ----
void p_lissajous(float u, const float* p, float t, float& x, float& y) {
    x = std::sin(p[0] * u + t);
    y = std::sin(p[1] * u);
}
void p_rose(float u, const float* p, float t, float& x, float& y) {
    const float r = std::cos(p[0] * u + t * 0.35f);
    x = r * std::cos(u);
    y = r * std::sin(u);
}
void p_heart(float u, const float* p, float t, float& x, float& y) {
    const float s = std::sin(u);
    const float k = p[0] * 0.0625f;   // 1/16
    x = k * 16.0f * s * s * s;
    y = k * (13.0f * std::cos(u) - 5.0f * std::cos(2.0f * u)
             - 2.0f * std::cos(3.0f * u) - std::cos(4.0f * u));
    // 缓慢自转，看起来像"活的"
    const float c = std::cos(t * 0.4f), sn = std::sin(t * 0.4f);
    const float rx = x * c - y * sn, ry = x * sn + y * c;
    x = rx; y = ry;
}
void p_superellipse(float u, const float* p, float t, float& x, float& y) {
    const float e = 2.0f / std::max(p[0], 0.05f);
    const float cu = std::cos(u), su = std::sin(u);
    x = std::copysign(std::pow(std::fabs(cu), e), cu);
    y = std::copysign(std::pow(std::fabs(su), e), su);
    const float c = std::cos(t * 0.3f), sn = std::sin(t * 0.3f);
    const float rx = x * c - y * sn, ry = x * sn + y * c;
    x = rx; y = ry;
}
void p_cycloid(float u, const float* p, float, float& x, float& y) {
    x = p[0] * (u - std::sin(u));
    y = p[0] * (1.0f - std::cos(u));
}

// ---- 极坐标 ----
float r_rose(float th, const float* p, float t)  { return std::cos(p[0] * th + t * 0.35f); }
float r_heart(float th, const float* p, float)   { return p[0] * (1.0f + std::sin(th)); }
float r_spiral(float th, const float* p, float)  { return p[0] * th; }
float r_lemniscate(float th, const float* p, float) {
    const float v = p[0] * std::cos(2.0f * th);
    return v > 0.0f ? std::sqrt(v) : std::nanf("");
}

// ---- 隐函数 F(x, y) = 0 ----
float F_heart(float x, float y, const float*, float) {
    const float a = x * x + y * y - 1.0f;
    return a * a * a - x * x * y * y * y;
}
float F_folium(float x, float y, const float* p, float) {
    return x * x * x + y * y * y - 3.0f * p[0] * x * y;
}
float F_lemniscate(float x, float y, const float* p, float) {
    const float r2 = x * x + y * y;
    return r2 * r2 - p[0] * p[0] * (x * x - y * y);
}
float F_ripple(float x, float y, const float* p, float t) {
    return std::sin(p[0] * x + t * 0.7f) * std::cos(p[1] * y) - 0.35f;
}
float F_astroid(float x, float y, const float* p, float) {
    return std::pow(std::fabs(x), 2.0f / 3.0f) + std::pow(std::fabs(y), 2.0f / 3.0f) - p[0];
}

// ---- 标量场 z = f(x, y) ----
float Z_ripple(float x, float y, const float* p, float t) {
    const float r = std::sqrt(x * x + y * y);
    return std::sin(p[0] * r - t) * std::exp(-0.12f * p[0] * r);
}
float Z_standing(float x, float y, const float* p, float t) {
    return std::sin(p[0] * x + t * 0.8f) * std::cos(p[1] * y);
}
float Z_dipole(float x, float y, const float* p, float t) {
    const float d = 1.2f + 0.35f * std::sin(t * 0.8f);
    const float d1 = (x - d) * (x - d) + y * y;
    const float d2 = (x + d) * (x + d) + y * y;
    return std::exp(-d1 / (p[0] * p[0])) - std::exp(-d2 / (p[0] * p[0]));
}

Preset mk_curve(const char* name, const char* formula, float half_h, float stretch,
                CurveFn fn, std::vector<Knob> knobs = {}) {
    Preset p;
    p.name = name; p.formula = formula; p.kind = Kind::Curve;
    p.half_h = half_h; p.stretch = stretch;
    p.curve = fn; p.knobs = std::move(knobs);
    return p;
}
Preset mk_param(const char* name, const char* formula, float half_h,
                ParamFn fn, float u0, float u1, std::vector<Knob> knobs = {}) {
    Preset p;
    p.name = name; p.formula = formula; p.kind = Kind::Param;
    p.half_h = half_h; p.stretch = 0.0f;
    p.param = fn; p.u0 = u0; p.u1 = u1; p.knobs = std::move(knobs);
    return p;
}
Preset mk_polar(const char* name, const char* formula, float half_h,
                CurveFn rfn, float u1, std::vector<Knob> knobs = {}) {
    Preset p;
    p.name = name; p.formula = formula; p.kind = Kind::Polar;
    p.half_h = half_h; p.stretch = 0.0f;
    p.curve = rfn; p.u0 = 0.0f; p.u1 = u1; p.knobs = std::move(knobs);
    return p;
}
Preset mk_implicit(const char* name, const char* formula, float half_h,
                   FieldFn fn, std::vector<Knob> knobs = {}) {
    Preset p;
    p.name = name; p.formula = formula; p.kind = Kind::Implicit;
    p.half_h = half_h; p.stretch = 0.0f;
    p.field = fn; p.knobs = std::move(knobs);
    return p;
}
Preset mk_field(const char* name, const char* formula, float half_h,
                FieldFn fn, std::vector<Knob> knobs = {}) {
    Preset p;
    p.name = name; p.formula = formula; p.kind = Kind::Field;
    p.half_h = half_h; p.stretch = 0.0f;
    p.field = fn; p.knobs = std::move(knobs);
    return p;
}

std::vector<Preset> build_presets(int cat) {
    std::vector<Preset> v;
    switch (cat) {
    case 0:   // 显函数
        v.push_back(mk_curve("正弦波", "y = sin(a*x - t)", 2.2f, 2.2f, f_sine,
                             {{"a 频率", 0.2f, 6.0f, 2.0f}}));
        v.push_back(mk_curve("阻尼振荡", "y = exp(-b*x^2) * sin(a*x - t)", 1.6f, 2.4f, f_damped,
                             {{"a 频率", 0.5f, 8.0f, 3.0f}, {"b 衰减", 0.02f, 0.5f, 0.12f}}));
        v.push_back(mk_curve("高斯钟形", "y = a * exp(-x^2 / b)", 1.8f, 2.6f, f_gauss,
                             {{"a 幅度", 0.2f, 2.0f, 1.0f}, {"b 宽度", 0.2f, 4.0f, 1.2f}}));
        v.push_back(mk_curve("双曲正切", "y = a * tanh(b*x)", 1.6f, 2.0f, f_tanh,
                             {{"a 幅度", 0.2f, 2.0f, 1.0f}, {"b 陡度", 0.2f, 4.0f, 1.5f}}));
        v.push_back(mk_curve("采样函数 sinc", "y = sin(a*x) / (a*x)", 1.4f, 2.0f, f_sinc,
                             {{"a 压缩", 0.5f, 14.0f, 5.0f}}));
        v.push_back(mk_curve("三次多项式", "y = x^3/8 - a*x", 3.0f, 1.6f, f_cubic,
                             {{"a 一次项", 0.0f, 4.0f, 2.0f}}));
        v.push_back(mk_curve("心电图", "y = a * ecg(x - t)", 1.6f, 3.0f, f_ecg,
                             {{"a 幅度", 0.3f, 2.0f, 1.0f}}));
        break;
    case 1:   // 参数方程
        v.push_back(mk_param("李萨如图形", "x = sin(a*u + t),  y = sin(b*u)", 1.3f, p_lissajous,
                             0.0f, kTau,
                             {{"a 水平频率", 1.0f, 7.0f, 3.0f}, {"b 垂直频率", 1.0f, 7.0f, 2.0f}}));
        v.push_back(mk_param("玫瑰线", "x = cos(a*u)*cos u,  y = cos(a*u)*sin u", 1.3f, p_rose,
                             0.0f, kTau, {{"a 花瓣", 1.0f, 9.0f, 4.5f}}));
        v.push_back(mk_param("心形线", "x = 16 sin^3 u,  y = 13 cos u - 5 cos 2u - 2 cos 3u - cos 4u",
                             1.5f, p_heart, 0.0f, kTau, {{"a 缩放", 0.6f, 1.6f, 1.0f}}));
        v.push_back(mk_param("超椭圆", "x = sgn(cos u)|cos u|^(2/a),  y = sgn(sin u)|sin u|^(2/a)",
                             1.35f, p_superellipse, 0.0f, kTau, {{"a 指数", 0.4f, 6.0f, 2.0f}}));
        v.push_back(mk_param("摆线", "x = a(u - sin u),  y = a(1 - cos u)", 2.0f, p_cycloid,
                             -3.0f * kPi, 3.0f * kPi, {{"a 半径", 0.1f, 0.6f, 0.3f}}));
        break;
    case 2:   // 极坐标
        v.push_back(mk_polar("玫瑰线", "r = cos(a*θ)", 1.25f, r_rose, kTau,
                             {{"a 花瓣", 0.5f, 9.0f, 4.0f}}));
        v.push_back(mk_polar("心形（心形线）", "r = a(1 + sin θ)", 2.4f, r_heart, kTau,
                             {{"a 大小", 0.3f, 2.0f, 1.0f}}));
        v.push_back(mk_polar("阿基米德螺线", "r = a*θ", 2.6f, r_spiral, 6.0f * kPi,
                             {{"a 疏密", 0.05f, 0.6f, 0.22f}}));
        v.push_back(mk_polar("双纽线", "r = sqrt(a*cos 2θ)", 1.5f, r_lemniscate, kTau,
                             {{"a 大小", 0.4f, 2.0f, 1.2f}}));
        break;
    case 3:   // 隐函数
        v.push_back(mk_implicit("心形（隐式）", "(x^2 + y^2 - 1)^3 - x^2*y^3 = 0", 1.6f, F_heart));
        v.push_back(mk_implicit("笛卡尔叶形线", "x^3 + y^3 - 3a*x*y = 0", 3.2f, F_folium,
                                {{"a 大小", 0.3f, 2.0f, 1.0f}}));
        v.push_back(mk_implicit("双纽线（隐式）", "(x^2 + y^2)^2 - a^2(x^2 - y^2) = 0", 1.6f, F_lemniscate,
                                {{"a 大小", 0.3f, 2.0f, 1.0f}}));
        v.push_back(mk_implicit("波纹网格", "sin(a*x) * cos(b*y) = 0.35", 4.0f, F_ripple,
                                {{"a 横向", 0.5f, 4.0f, 2.0f}, {"b 纵向", 0.5f, 4.0f, 2.0f}}));
        v.push_back(mk_implicit("星形线", "|x|^(2/3) + |y|^(2/3) = a", 1.6f, F_astroid,
                                {{"a 大小", 0.3f, 2.0f, 1.0f}}));
        break;
    case 4:   // 标量场
        v.push_back(mk_field("圆形波纹场", "z = sin(a*r - t) * exp(-0.12ar)", 8.0f, Z_ripple,
                             {{"a 波数", 0.5f, 5.0f, 2.2f}}));
        v.push_back(mk_field("驻波", "z = sin(a*x + t) * cos(b*y)", 4.0f, Z_standing,
                             {{"a 横向", 0.5f, 4.0f, 1.6f}, {"b 纵向", 0.5f, 4.0f, 1.6f}}));
        v.push_back(mk_field("双源干涉", "z = exp(-d1^2/b^2) - exp(-d2^2/b^2)", 2.6f, Z_dipole,
                             {{"b 半径", 0.3f, 3.0f, 1.0f}}));
        break;
    }
    return v;
}

// =============================================================================
//  三、绘图：世界坐标 ↔ 屏幕坐标
// =============================================================================

struct View {
    Rect  px;                       // 绘图区（屏幕逻辑坐标）
    float cx = 0.0f, cy = 0.0f;     // 视野中心（世界坐标）
    float half_w = 1.0f, half_h = 1.0f;

    float sx(float x) const { return px.cx() + (x - cx) * (px.w * 0.5f / half_w); }
    float sy(float y) const { return px.cy() - (y - cy) * (px.h * 0.5f / half_h); }
    float wx(float px_x) const { return cx + (px_x - px.cx()) * (half_w / (px.w * 0.5f)); }
    float wy(float px_y) const { return cy - (px_y - px.cy()) * (half_h / (px.h * 0.5f)); }
};

// Liang-Barsky：把线段裁到矩形内。为什么必须裁：raster_line 的代价是
// 整个包围盒，一条"从屏幕里飞到 1e6 外面"的线段会把整帧拖死。
bool clip_seg(float& x0, float& y0, float& x1, float& y1, const Rect& r) {
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - r.x, r.right() - x0, y0 - r.y, r.bottom() - y0};
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(p[i]) < 1e-9f) {
            if (q[i] < 0.0f) return false;
        } else {
            const float t = q[i] / p[i];
            if (p[i] < 0.0f) {
                if (t > t1) return false;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return false;
                if (t < t1) t1 = t;
            }
        }
    }
    const float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy;
    const float nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
}

void seg(aino::Ui& ui, const Rect& clip, aino::Vec2 a, aino::Vec2 b, const Color& c, float w) {
    float x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    if (!clip_seg(x0, y0, x1, y1, clip)) return;
    ui.draw_line(x0, y0, x1, y1, c, w);
}

// 折线：相邻两点连线；遇到 NaN / inf 就断开（双纽线、1/x 这类定义域断点）
void polyline(aino::Ui& ui, const Rect& clip, const std::vector<aino::Vec2>& pts,
              const Color& c, float w) {
    bool has_prev = false;
    aino::Vec2 prev;
    for (const aino::Vec2& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) { has_prev = false; continue; }
        if (has_prev) seg(ui, clip, prev, p, c, w);
        prev = p;
        has_prev = true;
    }
}

// 1-2-5 序列的"好看刻度"
float nice_step(float target) {
    if (!(target > 0.0f) || !std::isfinite(target)) return 1.0f;
    const float e = std::pow(10.0f, std::floor(std::log10(target)));
    const float f = target / e;
    const float m = f < 1.5f ? 1.0f : f < 3.5f ? 2.0f : f < 7.5f ? 5.0f : 10.0f;
    return m * e;
}

std::string tick_text(float v, float step) {
    if (std::fabs(v) < step * 1e-3f) return "0";
    if (step >= 1.0f) return fmt("%.0f", v);
    if (step >= 0.1f) return fmt("%.1f", v);
    if (step >= 0.01f) return fmt("%.2f", v);
    return fmt("%.3f", v);
}

// viridis 色带：感知均匀，深浅主题下都好看
Color viridis(float t) {
    static const float kStop[5][3] = {
        {0.267f, 0.005f, 0.329f}, {0.231f, 0.318f, 0.545f}, {0.128f, 0.569f, 0.551f},
        {0.369f, 0.788f, 0.383f}, {0.993f, 0.906f, 0.144f},
    };
    t = std::max(0.0f, std::min(1.0f, t)) * 4.0f;
    const int i = std::min(3, static_cast<int>(t));
    const float f = t - static_cast<float>(i);
    auto mix1 = [&](int ch) {
        const float v = kStop[i][ch] + (kStop[i + 1][ch] - kStop[i][ch]) * f;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    };
    return Color(mix1(0), mix1(1), mix1(2));
}

// =============================================================================
//  四、等值线（marching squares）
// =============================================================================

struct Grid {
    int nx = 0, ny = 0;
    float step = 1.0f;              // 逻辑像素
    float x0 = 0.0f, y0 = 0.0f;     // 网格左上角（屏幕逻辑坐标）
    std::vector<float> v;
    float at(int i, int j) const { return v[static_cast<size_t>(j) * (nx + 1) + i]; }
};

// 在格子里找等值线：4 条边上的交点两两相连。
// 4 个交点（鞍点）时用格子中心的值决定怎么配对 —— 不判的话会出现"交叉连线"。
void marching(const Grid& g, float iso,
              const std::function<void(float, float, float, float)>& emit) {
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const float a = g.at(i, j),     b = g.at(i + 1, j);
            const float d = g.at(i, j + 1), cc = g.at(i + 1, j + 1);
            const bool ia = a < iso, ib = b < iso, ic = cc < iso, id = d < iso;
            if (ia == ib && ib == ic && ic == id) continue;

            float px[4], py[4];
            int n = 0;
            const float gx = g.x0 + static_cast<float>(i) * g.step;
            const float gy = g.y0 + static_cast<float>(j) * g.step;
            if (ia != ib) { const float t = (iso - a) / (b - a); px[n] = gx + t * g.step; py[n] = gy; ++n; }
            if (ib != ic) { const float t = (iso - b) / (cc - b); px[n] = gx + g.step; py[n] = gy + t * g.step; ++n; }
            if (id != ic) { const float t = (iso - d) / (cc - d); px[n] = gx + t * g.step; py[n] = gy + g.step; ++n; }
            if (ia != id) { const float t = (iso - a) / (d - a); px[n] = gx; py[n] = gy + t * g.step; ++n; }

            if (n == 2) {
                emit(px[0], py[0], px[1], py[1]);
            } else if (n == 4) {
                const float center = (a + b + cc + d) * 0.25f;
                if (center < iso) {
                    emit(px[0], py[0], px[1], py[1]);
                    emit(px[2], py[2], px[3], py[3]);
                } else {
                    emit(px[0], py[0], px[3], py[3]);
                    emit(px[1], py[1], px[2], py[2]);
                }
            }
        }
    }
}

// =============================================================================
//  五、应用状态
// =============================================================================

struct App {
    int   cat = 0;              // 0..4 = 预设分类，5 = 自定义
    int   sel = 0;              // 当前分类里的第几个公式
    float p[3] = {1.0f, 1.0f, 0.0f};
    float zoom = 1.0f;
    float cx = 0.0f, cy = 0.0f;
    float t = 0.0f;             // 动画时钟
    bool  anim = true;
    bool  deriv = false;
    bool  contour = false;
    bool  dark = false;
    bool  dragging = false;

    // 自定义公式
    std::string custom_src  = "sin(a*x) * exp(-0.2*x*x)";
    std::string custom_src2 = "sin(a*u + 1.57)";     // 参数方程的第二行 y(u)
    int   custom_mode = 0;      // 0 显函数 / 1 参数 / 2 极坐标 / 3 隐式 / 4 标量场
    std::string custom_hist[5]; // 每个模式各自记着自己那份输入（切回来还在）
    std::string prog_for;       // 当前字节码对应的源码
    std::string prog2_for;
    fx::Program prog;
    fx::Program prog2;

    // 每个模式的默认公式：切过去第一眼就该是个能看的图形
    static const char* default_src(int m) {
        switch (m) {
        case 1:  return "cos(a*u) + cos(5*u)/5";
        case 2:  return "1 + cos(a*x)";
        case 3:  return "x^2 + y^2 = 1";
        case 4:  return "sin(a*hypot(x,y))";
        default: return "sin(a*x) * exp(-0.2*x*x)";
        }
    }
    // 切换模式：存走旧的、载入新的（没写过就用该模式的默认公式）
    void switch_mode(int m) {
        if (m == custom_mode) return;
        custom_hist[custom_mode] = custom_src;
        custom_src = custom_hist[m].empty() ? default_src(m) : custom_hist[m];
        custom_mode = m;
        prog_for.clear();
        prog2_for.clear();
    }
};

constexpr int kCustomCat = 5;
const char* kCatNames[] = {
    "曲线  y = f(x)", "参数方程", "极坐标  r = f(θ)",
    "隐函数  F(x,y) = 0", "标量场  z = f(x,y)", "自定义公式"
};

// =============================================================================
//  六、绘制一帧
// =============================================================================

struct Ctx {
    aino::Ui& ui;
    const aino::Theme& th;
    const View& v;
    const Preset& pre;
    Kind  mode;
    const fx::Program* prog;    // 自定义时非空
    const fx::Program* prog2;   // 参数方程的第二行 y(u)
    const float* p;
    float t;
    bool  dark;
    bool  deriv = false;        // 是否叠加导数曲线
    bool  contour = false;      // 场模式：等高线 vs 热力图
    Color curve_c;
};

float eval_curve(const Ctx& c, float x) {
    if (c.prog) return fx::eval(*c.prog, x, 0.0f, c.t, c.p[0], c.p[1], c.p[2]);
    return c.pre.curve(x, c.p, c.t);
}
float eval_r(const Ctx& c, float th) {
    if (c.prog) return fx::eval(*c.prog, th, 0.0f, c.t, c.p[0], c.p[1], c.p[2]);
    return c.pre.curve(th, c.p, c.t);
}
float eval_F(const Ctx& c, float x, float y) {
    if (c.prog) return fx::eval(*c.prog, x, y, c.t, c.p[0], c.p[1], c.p[2]);
    return c.pre.field(x, y, c.p, c.t);
}

// 网格和坐标轴：画在曲线下面
void draw_grid(aino::Ui& ui, const View& v, const aino::Theme& th) {
    const Rect& px = v.px;
    const Color grid = th.border.with_alpha(150);
    const Color axis = th.muted_foreground.with_alpha(190);

    const float step = nice_step(v.half_w * 2.0f / 8.0f);
    const float step_y = nice_step(v.half_h * 2.0f / 6.0f);

    for (float x = std::ceil((v.cx - v.half_w) / step) * step; x <= v.cx + v.half_w + 1e-5f; x += step) {
        const float s = v.sx(x);
        if (s < px.x - 1.0f || s > px.right() + 1.0f) continue;
        ui.draw_line(s, px.y, s, px.bottom(), grid, 1.0f);
    }
    for (float y = std::ceil((v.cy - v.half_h) / step_y) * step_y; y <= v.cy + v.half_h + 1e-5f; y += step_y) {
        const float s = v.sy(y);
        if (s < px.y - 1.0f || s > px.bottom() + 1.0f) continue;
        ui.draw_line(px.x, s, px.right(), s, grid, 1.0f);
    }

    const float ax = v.sx(0.0f), ay = v.sy(0.0f);
    if (ay >= px.y && ay <= px.bottom()) ui.draw_line(px.x, ay, px.right(), ay, axis, 1.3f);
    if (ax >= px.x && ax <= px.right()) ui.draw_line(ax, px.y, ax, px.bottom(), axis, 1.3f);
}

// 刻度数字：画在曲线之上。为什么分开：曲线横穿坐标轴的位置正好是刻度所在，
// 先画数字会被曲线糊掉（深色主题下尤其明显）。
void draw_ticks(aino::Ui& ui, const View& v, const aino::Theme& th) {
    const Rect& px = v.px;
    const float step = nice_step(v.half_w * 2.0f / 8.0f);
    const float step_y = nice_step(v.half_h * 2.0f / 6.0f);
    const float ax = v.sx(0.0f), ay = v.sy(0.0f);
    const Color tc = th.muted_foreground.with_alpha(238);
    const float fs = 10.5f;

    // 轴跑出视野就贴到边上，别把数字画丢
    const bool x_in = (ay >= px.y + 2.0f && ay <= px.bottom() - 18.0f);
    const float ly = x_in ? ay + 2.0f : px.bottom() - 15.0f;
    for (float x = std::ceil((v.cx - v.half_w) / step) * step; x <= v.cx + v.half_w + 1e-5f; x += step) {
        if (std::fabs(x) < step * 0.4f) continue;
        const std::string s = tick_text(x, step);
        const float sxx = v.sx(x);
        const float w = ui.text_width(s, fs);
        if (sxx - w * 0.5f < px.x + 2.0f || sxx + w * 0.5f > px.right() - 2.0f) continue;
        ui.draw_text(s, Rect(sxx - w * 0.5f, ly, w + 2.0f, 13.0f), tc,
                     Style().font(fs).left().top());
    }
    const bool y_in = (ax >= px.x + 44.0f && ax <= px.right() - 2.0f);
    for (float y = std::ceil((v.cy - v.half_h) / step_y) * step_y; y <= v.cy + v.half_h + 1e-5f; y += step_y) {
        if (std::fabs(y) < step_y * 0.4f) continue;
        const std::string s = tick_text(y, step_y);
        const float syy = v.sy(y);
        const float w = ui.text_width(s, fs);
        const float rx = y_in ? ax - 5.0f : px.x + 40.0f;
        if (syy < px.y + 2.0f || syy > px.bottom() - 12.0f) continue;
        ui.draw_text(s, Rect(rx - w, syy - 7.0f, w + 2.0f, 13.0f), tc,
                     Style().font(fs).right().top());
    }
    // 原点
    if (ax >= px.x && ax <= px.right() && ay >= px.y && ay <= px.bottom()) {
        ui.draw_text("0", Rect(ax - 12.0f, ay + 2.0f, 10.0f, 13.0f), tc,
                     Style().font(fs).right().top());
    }
}

void draw_curve(Ctx& c) {
    aino::Ui& ui = c.ui;
    const View& v = c.v;
    const Rect& px = v.px;
    const int n = std::max(64, static_cast<int>(px.w * 1.4f));
    std::vector<aino::Vec2> pts;
    pts.reserve(static_cast<size_t>(n) + 1);

    // 按"屏幕像素"采样而不是按世界单位：这样无论缩放到哪一级，
    // 曲线密度都是一致的（也不会有长线段把包围盒撑大）。
    for (int i = 0; i <= n; ++i) {
        const float x = v.wx(px.x + px.w * static_cast<float>(i) / static_cast<float>(n));
        const float y = eval_curve(c, x);
        pts.push_back(aino::Vec2(v.sx(x), v.sy(y)));
    }

    // 光晕（宽、低 alpha）+ 实线：两次折线，视觉上"发光"但不糊
    polyline(ui, px, pts, c.curve_c.with_alpha(26), 6.0f);

    if (c.pre.kind == Kind::Curve && !c.prog && c.th.background.luminance() < 0.5f) {
        // 深色主题下给曲线一层更明显的辉光，看起来像发光的示波器
        polyline(ui, px, pts, c.curve_c.with_alpha(40), 3.6f);
    }
    polyline(ui, px, pts, c.curve_c, 2.1f);

    // 数值导数（只对显函数有意义）
    if (c.deriv && c.mode == Kind::Curve) {
        const Color dc = c.dark ? Color::hex(0xFB923C) : Color::hex(0xEA580C);
        std::vector<aino::Vec2> d;
        d.reserve(static_cast<size_t>(n) + 1);
        const float h = v.half_w * 2.0f / static_cast<float>(n) * 0.5f;
        for (int i = 0; i <= n; ++i) {
            const float x = v.wx(px.x + px.w * static_cast<float>(i) / static_cast<float>(n));
            const float y0 = eval_curve(c, x - h), y1 = eval_curve(c, x + h);
            d.push_back(aino::Vec2(v.sx(x), v.sy((y1 - y0) / (2.0f * h))));
        }
        polyline(ui, px, d, dc.with_alpha(210), 1.5f);
    }
}

void draw_parametric(Ctx& c) {
    const View& v = c.v;
    const Rect& px = v.px;
    const int n = std::max(200, std::min(1800, static_cast<int>(px.w * 2.0f)));
    std::vector<aino::Vec2> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const float u = c.pre.u0 + (c.pre.u1 - c.pre.u0) * static_cast<float>(i) / static_cast<float>(n);
        float x = 0.0f, y = 0.0f;
        if (c.prog2) {
            // 自定义参数方程：两行表达式各算一个坐标，变量是 u
            x = fx::eval(*c.prog, u, 0.0f, c.t, c.p[0], c.p[1], c.p[2]);
            y = fx::eval(*c.prog2, u, 0.0f, c.t, c.p[0], c.p[1], c.p[2]);
        } else {
            c.pre.param(u, c.p, c.t, x, y);
        }
        pts.push_back(aino::Vec2(v.sx(x), v.sy(y)));
    }
    polyline(c.ui, px, pts, c.curve_c.with_alpha(30), 6.0f);
    polyline(c.ui, px, pts, c.curve_c, 2.1f);
}

void draw_polar(Ctx& c) {
    const View& v = c.v;
    const Rect& px = v.px;
    const int n = std::max(240, std::min(2000, static_cast<int>(px.w * 2.0f)));
    std::vector<aino::Vec2> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const float th = c.pre.u0 + (c.pre.u1 - c.pre.u0) * static_cast<float>(i) / static_cast<float>(n);
        const float r = eval_r(c, th);
        pts.push_back(aino::Vec2(v.sx(r * std::cos(th)), v.sy(r * std::sin(th))));
    }
    polyline(c.ui, px, pts, c.curve_c.with_alpha(30), 6.0f);
    polyline(c.ui, px, pts, c.curve_c, 2.1f);
}

// 采样一张标量场网格（隐函数 / 场共用）
Grid sample_grid(const Ctx& c, float cell) {
    const Rect& px = c.v.px;
    Grid g;
    g.step = cell;
    g.x0 = px.x;
    g.y0 = px.y;
    g.nx = std::max(2, static_cast<int>(std::ceil(px.w / cell)));
    g.ny = std::max(2, static_cast<int>(std::ceil(px.h / cell)));
    g.v.resize(static_cast<size_t>(g.nx + 1) * static_cast<size_t>(g.ny + 1));

    for (int j = 0; j <= g.ny; ++j) {
        const float y = c.v.wy(px.y + static_cast<float>(j) * cell);
        for (int i = 0; i <= g.nx; ++i) {
            const float x = c.v.wx(px.x + static_cast<float>(i) * cell);
            float val = eval_F(c, x, y);
            if (!std::isfinite(val)) val = 1e9f;    // NaN 当"正无穷"处理，别让交点插值炸掉
            g.v[static_cast<size_t>(j) * (g.nx + 1) + i] = val;
        }
    }
    return g;
}

void draw_implicit(Ctx& c) {
    const Grid g = sample_grid(c, 5.0f);
    const Color halo = c.curve_c.with_alpha(24);
    marching(g, 0.0f, [&](float x0, float y0, float x1, float y1) {
        seg(c.ui, c.v.px, aino::Vec2(x0, y0), aino::Vec2(x1, y1), halo, 5.0f);
    });
    marching(g, 0.0f, [&](float x0, float y0, float x1, float y1) {
        seg(c.ui, c.v.px, aino::Vec2(x0, y0), aino::Vec2(x1, y1), c.curve_c, 2.0f);
    });
}

void draw_field(Ctx& c) {
    aino::Ui& ui = c.ui;
    const Rect& px = c.v.px;
    const Grid g = sample_grid(c, c.contour ? 5.0f : 4.0f);

    if (!c.contour) {
        // 热力图：颜色量化到 96 级，再把同一行里同色的格子合成一条矩形。
        // 逐格 fill_rect 是两万多画，合并后通常只剩两三千 —— 这是这个页面
        // 能在 60fps 下拖动的主要原因之一。级数太少会出现明显的"色阶环"。
        const int LV = 96;
        auto level_of = [&](float z) {
            return std::max(0, std::min(LV - 1, static_cast<int>((z + 1.0f) * 0.5f * LV)));
        };
        for (int j = 0; j < g.ny; ++j) {
            int run_start = 0;
            int run_lv = level_of(g.at(0, j));
            for (int i = 1; i <= g.nx; ++i) {
                const int lv = (i < g.nx) ? level_of(g.at(i, j)) : -999;
                if (lv != run_lv) {
                    if (run_lv >= 0) {
                        const float x = px.x + static_cast<float>(run_start) * g.step;
                        const float y = px.y + static_cast<float>(j) * g.step;
                        const float w = static_cast<float>(i - run_start) * g.step + 0.7f;
                        ui.fill_rect(Rect(x, y, w, g.step + 0.7f),
                                     viridis((static_cast<float>(run_lv) + 0.5f) / static_cast<float>(LV)));
                    }
                    run_start = i;
                    run_lv = lv;
                }
            }
        }
    } else {
        float zmin = 1e9f, zmax = -1e9f;
        for (float z : g.v) { zmin = std::min(zmin, z); zmax = std::max(zmax, z); }
        zmin = std::max(zmin, -1.0f);
        zmax = std::min(zmax, 1.0f);
        if (zmax - zmin < 0.2f) { zmin = -1.0f; zmax = 1.0f; }
        const int levels = 11;
        for (int k = 1; k < levels; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(levels);
            const float iso = zmin + (zmax - zmin) * t;
            const Color col = viridis(t);
            marching(g, iso, [&](float x0, float y0, float x1, float y1) {
                seg(ui, px, aino::Vec2(x0, y0), aino::Vec2(x1, y1), col, 1.4f);
            });
        }
    }
}

// 场模式的色标
void draw_colorbar(aino::Ui& ui, const View& v, const aino::Theme& th) {
    const Rect px = v.px;
    const float bw = 96.0f, bh = 7.0f;
    const Rect bar(px.x + 26.0f, px.bottom() - 22.0f, bw, bh);
    ui.fill_rect(Rect(bar.x - 22.0f, bar.y - 6.0f, bw + 46.0f, bh + 12.0f),
                 th.background.with_alpha(228), 5.0f);
    ui.stroke_rect(Rect(bar.x - 22.0f, bar.y - 6.0f, bw + 46.0f, bh + 12.0f),
                   th.border.with_alpha(160), 5.0f, 1.0f);
    for (int i = 0; i < 32; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / 32.0f;
        ui.fill_rect(Rect(bar.x + bw * static_cast<float>(i) / 32.0f, bar.y,
                          bw / 32.0f + 0.5f, bh), viridis(t));
    }
    ui.draw_text("-1", Rect(bar.x - 20.0f, bar.y - 2.0f, 17.0f, 11.0f), th.muted_foreground,
                 Style().font(10.0f).right().middle());
    ui.draw_text("+1", Rect(bar.right() + 4.0f, bar.y - 2.0f, 17.0f, 11.0f), th.muted_foreground,
                 Style().font(10.0f).left().middle());
}

void draw_crosshair(Ctx& c) {
    aino::Ui& ui = c.ui;
    const View& v = c.v;
    const Rect& px = v.px;
    const aino::Input& in = ui.input();
    if (!px.contains(in.mouse)) return;

    const float mx = in.mouse.x, my = in.mouse.y;
    const Color ch = c.dark ? Color::hex(0x9CA3AF) : Color::hex(0x71717A);
    // 手画虚线：4px 实 / 4px 空。没有 dash 图元，就自己切段
    for (float y = px.y; y < px.bottom(); y += 8.0f)
        ui.draw_line(mx, y, mx, std::min(y + 4.0f, px.bottom()), ch.with_alpha(110), 1.0f);
    for (float x = px.x; x < px.right(); x += 8.0f)
        ui.draw_line(x, my, std::min(x + 4.0f, px.right()), my, ch.with_alpha(110), 1.0f);

    const float wxp = v.wx(mx), wyp = v.wy(my);
    std::string info;
    float dot_x = 0.0f, dot_y = 0.0f;
    bool has_dot = false;

    switch (c.mode) {
    case Kind::Curve: {
        const float y = eval_curve(c, wxp);
        if (std::isfinite(y)) {
            dot_x = v.sx(wxp);
            dot_y = v.sy(y);
            has_dot = true;
            info = fmt("x %+.3f    y %+.3f", wxp, y);
        } else {
            info = fmt("x %+.3f    y 无定义", wxp);
        }
        break;
    }
    case Kind::Polar: {
        const float th = std::atan2(wyp, wxp);
        float tt = th;
        if (tt < 0.0f) tt += kTau;
        const float r = eval_r(c, tt);
        info = fmt("θ %.3f    r %+.3f", tt, r);
        break;
    }
    case Kind::Param:
        info = fmt("x %+.3f    y %+.3f", wxp, wyp);
        break;
    case Kind::Implicit: {
        const float f = eval_F(c, wxp, wyp);
        info = fmt("x %+.3f  y %+.3f   F %+.3f", wxp, wyp, f);
        break;
    }
    case Kind::Field: {
        const float z = eval_F(c, wxp, wyp);
        info = fmt("x %+.3f  y %+.3f   z %+.3f", wxp, wyp, z);
        break;
    }
    }

    if (has_dot) {
        // 先垫一层背景色，再画实心点：曲线正好穿过这里，不垫就看不出点在哪
        ui.fill_circle(dot_x, dot_y, 5.2f, c.th.background);
        ui.fill_circle(dot_x, dot_y, 4.0f, c.curve_c.with_alpha(70));
        ui.fill_circle(dot_x, dot_y, 2.8f, c.curve_c);
    }

    const float fs = 11.0f;
    const float w = ui.text_width(info, fs) + 20.0f;
    const Rect chip(px.x + 10.0f, px.y + 10.0f, std::max(140.0f, w), 24.0f);
    ui.fill_rect(chip, c.th.background.with_alpha(232), 6.0f);
    ui.stroke_rect(chip, c.th.border, 6.0f, 1.0f);
    ui.draw_text(info, chip.inset(10.0f, 0.0f), c.th.foreground, Style().font(fs).left().middle());
}

void draw_legend(Ctx& c) {
    aino::Ui& ui = c.ui;
    if (!(c.deriv && c.mode == Kind::Curve)) return;
    const Rect& px = c.v.px;
    const Color dc = c.dark ? Color::hex(0xFB923C) : Color::hex(0xEA580C);
    const float y = px.y + 14.0f;
    ui.fill_circle(px.right() - 118.0f, y + 3.0f, 3.2f, c.curve_c);
    ui.draw_text("y", Rect(px.right() - 110.0f, y - 5.0f, 20.0f, 16.0f), c.th.muted_foreground,
                 Style().font(11.0f).left().middle());
    ui.fill_circle(px.right() - 62.0f, y + 3.0f, 3.2f, dc);
    ui.draw_text("y'", Rect(px.right() - 54.0f, y - 5.0f, 30.0f, 16.0f), c.th.muted_foreground,
                 Style().font(11.0f).left().middle());
}

// =============================================================================
//  七、界面
// =============================================================================
struct UiState {
    App app;
    aino::Window* win = nullptr;
    std::vector<Preset> presets;      // 当前分类的公式
    int cached_cat = -1;

    void reset_view() {
        app.zoom = 1.0f;
        app.cx = app.cy = 0.0f;
    }
    // 切分类 / 切公式：视野复位，参数回到这个公式的默认值
    void reset_params() {
        app.p[0] = app.p[1] = app.p[2] = 0.0f;
        if (app.cat == kCustomCat) {
            app.p[0] = 1.0f; app.p[1] = 1.0f; app.p[2] = 0.0f;
            return;
        }
        if (presets.empty()) return;
        app.sel = std::max(0, std::min(static_cast<int>(presets.size()) - 1, app.sel));
        const Preset& pre = presets[app.sel];
        for (size_t i = 0; i < pre.knobs.size() && i < 3; ++i) app.p[i] = pre.knobs[i].def;
    }
    void refresh() {
        if (cached_cat == app.cat) return;
        cached_cat = app.cat;
        presets = build_presets(app.cat);
    }
};

void draw_sidebar(aino::Ui& ui, UiState& st, const aino::Theme& th) {
    App& a = st.app;
    const float W = ui.width(), H = ui.height();
    const float SIDE = 232.0f;

    ui.fill_rect(Rect(0.0f, 0.0f, SIDE, H), th.background.mix(th.muted, 0.55f));
    ui.fill_rect(Rect(SIDE - 1.0f, 0.0f, 1.0f, H), th.border);

    aino::Stack col = ui.vbox(Rect(0.0f, 0.0f, SIDE, H), 6.0f, 18.0f);
    ui.heading("函数显示器", col.next(28.0f), Style().font(17.0f).left());
    ui.muted_text("aino · 单文件绘图", col.next(16.0f), Style().font(11.5f).left());
    col.skip(8.0f);

    int cat = a.cat;
    std::vector<std::string> cats(kCatNames, kCatNames + 6);
    if (ui.select(cats, &cat, col.next(34.0f))) {
        a.cat = cat;
        a.sel = 0;
        st.reset_view();
        st.refresh();
        st.reset_params();
    }
    col.skip(6.0f);
    ui.separator(col.next(1.0f));
    col.skip(4.0f);

    if (a.cat == kCustomCat) {
        ui.muted_text("在下方输入框里手打一个公式", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("运算：+ - * / ^ %  |x|", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("省略乘号：2x  3(x+1)", col.next(16.0f), Style().font(11.5f).left());
        col.skip(6.0f);
        ui.muted_text("变量：x y u t a b c", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("常量：pi tau e phi", col.next(16.0f), Style().font(11.5f).left());
        col.skip(6.0f);
        ui.muted_text("三角 sin cos tan sec", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     csc cot asin acos", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     atan atan2", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("双曲 sinh cosh tanh", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     asinh acosh atanh", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("指数 exp log log10", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     log2 sqrt cbrt", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("取整 floor ceil round", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     trunc frac sign", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("其它 min max pow mod", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     hypot abs step", col.next(16.0f), Style().font(11.5f).left());
        ui.muted_text("     clamp lerp smoothstep", col.next(16.0f), Style().font(11.5f).left());
    } else {
        for (size_t i = 0; i < st.presets.size(); ++i) {
            if (col.remain() < 34.0f) break;
            if (ui.nav_item(st.presets[i].name, a.sel == static_cast<int>(i), col.next(30.0f))) {
                a.sel = static_cast<int>(i);
                st.reset_view();
                st.reset_params();
            }
        }
    }

    // 底部：主题开关 + 运行状态
    const Rect foot(18.0f, H - 56.0f, SIDE - 36.0f, 26.0f);
    ui.draw_text("深色主题", Rect(foot.x, foot.y, 90.0f, 26.0f), th.foreground,
                 Style().font(12.5f).left().middle());
    bool dark = a.dark;
    if (ui.toggle(&dark, Rect(foot.right() - 44.0f, foot.y + 1.0f, 44.0f, 24.0f))) {
        a.dark = dark;
        if (st.win) st.win->set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
    }
    // 状态行：动画开着是 60fps 连续重绘，关掉就回到"一帧都不画"
    ui.draw_text(a.anim ? "动画中 60fps · 空格暂停" : "已暂停 · 空闲 0 帧",
                 Rect(18.0f, H - 26.0f, SIDE - 36.0f, 14.0f), th.muted_foreground,
                 Style().font(10.5f).left().middle());
}

void draw_controls(aino::Ui& ui, UiState& st, const Rect& bar, const aino::Theme& th,
                   Kind mode, const Preset& pre) {
    App& a = st.app;
    const bool custom = (a.cat == kCustomCat);
    ui.panel(bar);

    const Rect inner = bar.inset(16.0f, 14.0f);
    float y = inner.y;

    if (custom) {
        // 每种模式给一句对症的示例 —— 光写 "支持表达式" 没人知道能写什么
        auto input_row = [&](const char* tag, std::string* s, const char* hint) {
            aino::Stack hb = ui.hbox(Rect(inner.x, y, inner.w, 36.0f), 10.0f);
            ui.draw_text(tag, hb.next(52.0f), th.muted_foreground,
                         Style().font(12.0f).left().middle());
            ui.input(s, hb.fill(), Style().hint(hint));
            y += 36.0f + 10.0f;
        };
        switch (a.custom_mode) {
        case 0: input_row("y =", &a.custom_src, "例如  sin(a*x) * exp(-0.2*x*x)"); break;
        case 1:
            input_row("x(u) =", &a.custom_src,  "例如  cos(a*u) + cos(5*u)/5");
            input_row("y(u) =", &a.custom_src2, "例如  sin(a*u) + sin(5*u)/5");
            break;
        case 2: input_row("r =", &a.custom_src, "例如  1 + cos(a*x)    θ 就写 x（或 th）"); break;
        case 3: input_row("F =", &a.custom_src, "例如  x^2 + y^2 = 1（等号两侧都行）"); break;
        default: input_row("z =", &a.custom_src, "例如  sin(a*hypot(x,y))"); break;
        }
    }

    // 参数滑杆：固定按 3 列切，切换公式时位置不变 —— 组件 id 由位置算，
    // 位置不变状态就不会丢（见 docs/06 的 0.1 节）。
    {
        const Rect row(inner.x, y, inner.w, 34.0f);
        const int n = custom ? 3 : static_cast<int>(pre.knobs.size());
        if (n == 0) {
            ui.draw_text("这个公式没有可调参数 —— 滚轮缩放、拖拽平移、双击复位",
                         Rect(row.x, row.y, row.w, 34.0f), th.muted_foreground.with_alpha(210),
                         Style().font(11.5f).left().middle());
        }
        // 每格限宽 300：窗口拉宽时滑杆不该拉到一米长，短滑杆更好瞄
        const float gapw = 26.0f;
        const float cellw = std::min((row.w - gapw * (n - 1)) / std::max(n, 1), 300.0f);
        for (int i = 0; i < n; ++i) {
            const Rect cell(row.x + static_cast<float>(i) * (cellw + gapw), row.y, cellw, row.h);
            const std::string label = custom ? fmt("%c", 'a' + i) : pre.knobs[i].label;
            const float lo = custom ? -3.0f : pre.knobs[i].lo;
            const float hi = custom ? 3.0f : pre.knobs[i].hi;
            ui.draw_text(label, Rect(cell.x, cell.y, cell.w - 44.0f, 14.0f), th.muted_foreground,
                         Style().font(11.5f).left().top());
            ui.draw_text(fmt("%+.2f", a.p[i]), Rect(cell.x, cell.y, cell.w, 14.0f),
                         th.foreground, Style().font(11.5f).right().top());
            ui.slider(&a.p[i], lo, hi, Rect(cell.x, cell.y + 17.0f, cell.w, 18.0f));
        }
        y += 34.0f + 10.0f;
    }

    // 开关行
    const Rect row(inner.x, inner.bottom() - 32.0f, inner.w, 32.0f);
    aino::Stack hb = ui.hbox(row, 8.0f);
    ui.toggle_button("动画", &a.anim, hb.next(66.0f));
    if (mode == Kind::Curve)
        ui.toggle_button("导数 y'", &a.deriv, hb.next(82.0f));
    if (mode == Kind::Field)
        ui.toggle_button("等高线", &a.contour, hb.next(80.0f));
    if (custom) {
        int nm = a.custom_mode;
        if (ui.segmented({"曲线", "参数", "极坐标", "隐式", "标量场"}, &nm, hb.next(236.0f)))
            a.switch_mode(nm);
    }

    const Rect rb(row.right() - 92.0f, row.y, 92.0f, 32.0f);
    if (ui.button("复位视图", rb, Style().outline()))
        st.reset_view();
}

void draw_plot(aino::Ui& ui, UiState& st, const Rect& card, const aino::Theme& th) {
    App& a = st.app;
    const bool dark = th.background.luminance() < 0.5f;
    ui.panel(card);

    const Rect px = card.inset(12.0f);
    if (px.w < 40.0f || px.h < 40.0f) return;

    const bool custom = (a.cat == kCustomCat);
    const bool custom_field = custom && a.custom_mode == 4;

    // 浅色主题下 card 和 background 是同一个白，再铺一遍纯属白烧 44 万像素；
    // 场模式整块都会被热力图画满，也不用先铺底。
    const bool field_mode = custom_field || (st.presets.empty() ? false : st.presets[std::max(0, std::min(
                                static_cast<int>(st.presets.size()) - 1, a.sel))].kind == Kind::Field);
    if (field_mode || !(th.background == th.card)) ui.fill_rect(px, th.background);

    // 自定义公式：源码变了就重编译（每帧只比一次字符串，开销可忽略）
    if (custom) {
        if (a.prog_for != a.custom_src) {
            a.prog_for = a.custom_src;
            fx::compile(a.custom_src, a.prog);
        }
        if (a.custom_mode == 1 && a.prog2_for != a.custom_src2) {
            a.prog2_for = a.custom_src2;
            fx::compile(a.custom_src2, a.prog2);
        }
    }

    // 当前预设 / 模式
    const Preset* pre_p = nullptr;
    Preset custom_pre;
    if (custom) {
        static const Kind kKindOf[5] = {Kind::Curve, Kind::Param, Kind::Polar,
                                        Kind::Implicit, Kind::Field};
        static const float kHalfOf[5] = {3.0f, 2.0f, 2.0f, 2.2f, 1.8f};
        const int m = std::max(0, std::min(4, a.custom_mode));
        custom_pre.name = "自定义公式";
        custom_pre.formula = (m == 1) ? ("x = " + a.custom_src + "    y = " + a.custom_src2)
                                      : a.custom_src;
        custom_pre.kind = kKindOf[m];
        custom_pre.half_h = kHalfOf[m];
        custom_pre.stretch = (m == 0) ? 2.4f : 0.0f;
        pre_p = &custom_pre;
    } else {
        if (st.presets.empty()) return;
        a.sel = std::max(0, std::min(static_cast<int>(st.presets.size()) - 1, a.sel));
        pre_p = &st.presets[a.sel];
    }
    const Preset& pre = *pre_p;

    // 视野由「预设的默认半高 × 缩放」和「平移量」共同决定。
    // 等比例模式下 half_w 由视口宽高比推出（圆看起来才是圆）；
    // 显函数用固定的 stretch，横向给得更宽（sin/cos 需要）。
    auto make_view = [&]() {
        View mv;
        mv.px = px;
        mv.cx = a.cx;
        mv.cy = a.cy;
        const float hh = pre.half_h * a.zoom;
        const float aspect = px.w / std::max(px.h, 1.0f);
        mv.half_h = hh;
        mv.half_w = hh * (pre.stretch > 0.0f ? pre.stretch : aspect);
        return mv;
    };

    // ---- 交互：滚轮缩放 / 拖拽平移 / 双击复位 ----
    View v = make_view();
    const aino::Input& in = ui.input();
    const bool inside = px.contains(in.mouse);
    if (inside && std::fabs(in.wheel) > 0.001f) {
        const float mx = in.mouse.x, my = in.mouse.y;
        const float wx = v.wx(mx), wy = v.wy(my);
        a.zoom = std::max(0.04f, std::min(40.0f, a.zoom * std::pow(0.88f, in.wheel)));
        // 让光标下的那个世界点留在原处 —— 缩放才是"对着看的地方放大"
        const View nv = make_view();
        a.cx = wx - (mx - px.cx()) * (nv.half_w / (px.w * 0.5f));
        a.cy = wy + (my - px.cy()) * (nv.half_h / (px.h * 0.5f));
    }
    if (inside && in.pressed[0]) a.dragging = true;
    if (!in.down[0]) a.dragging = false;
    if (a.dragging) {
        a.cx -= in.mouse_delta.x * (v.half_w / (px.w * 0.5f));
        a.cy += in.mouse_delta.y * (v.half_h / (px.h * 0.5f));
    }
    if (in.double_click && inside) st.reset_view();
    if (in.key_pressed[aino::KEY_SPACE]) a.anim = !a.anim;
    if (in.key_pressed['R']) st.reset_view();
    if (a.dragging) ui.set_cursor(aino::Cursor::Hand);

    v = make_view();   // 交互改过 zoom / cx / cy，画之前重算一次

    const bool ok1 = !custom || a.prog.ok;
    const bool ok2 = !custom || a.custom_mode != 1 || a.prog2.ok;
    Ctx c{ui, th, v, pre, pre.kind,
          (custom && a.prog.ok) ? &a.prog : nullptr,
          (custom && a.custom_mode == 1 && a.prog2.ok) ? &a.prog2 : nullptr,
          a.p, a.t, dark, a.deriv, a.contour,
          dark ? Color::hex(0x60A5FA) : Color::hex(0x2563EB)};
    if (pre.kind == Kind::Field) c.curve_c = dark ? Color::hex(0x34D399) : Color::hex(0x059669);

    // ---- 绘制 ----
    ui.push_clip(px);
    // 场模式是一整张"图片"，网格和刻度会互相盖，只留色标和准星读数
    const bool is_field = (pre.kind == Kind::Field);
    if (!is_field) draw_grid(ui, v, th);
    switch (pre.kind) {
    case Kind::Curve:    if (ok1) draw_curve(c); break;
    case Kind::Param:    if (ok1 && ok2) draw_parametric(c); break;
    case Kind::Polar:    if (ok1) draw_polar(c); break;
    case Kind::Implicit: if (ok1) draw_implicit(c); break;
    case Kind::Field:    if (ok1) draw_field(c); break;
    }
    if (!is_field) draw_ticks(ui, v, th);
    draw_legend(c);
    draw_crosshair(c);
    if (is_field) draw_colorbar(ui, v, th);
    ui.pop_clip();
    ui.stroke_rect(px, th.border, 0.0f, 1.0f);

    // 自定义公式的语法错误提示（参数方程两行分别报，指出是哪一行）
    {
        const char* bad = nullptr;
        std::string msg;
        if (custom && !a.prog.ok) { bad = "表达式错误："; msg = a.prog.error; }
        else if (custom && a.custom_mode == 1 && !a.prog2.ok) {
            bad = "y(u) 有错误："; msg = a.prog2.error;
        }
        if (bad) {
            const Rect box(px.x + 12.0f, px.y + 12.0f, std::min(360.0f, px.w - 24.0f), 34.0f);
            ui.fill_rect(box, th.destructive.with_alpha(24), 6.0f);
            ui.stroke_rect(box, th.destructive.with_alpha(90), 6.0f, 1.0f);
            ui.draw_text(bad + msg, box.inset(10.0f, 0.0f), th.destructive,
                         Style().font(12.0f).left().middle());
        }
    }
}

} // namespace

// =============================================================================
//  八、表达式自测
//
//  WIN32 子系统的 exe 没有控制台，所以自测结果写进文件：
//      AINO_FN_SELFTEST=out.txt function_viewer.exe
//  返回 0 = 全过，1 = 有失败。
// =============================================================================
int run_selftest(const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) f = stdout;
    int fails = 0;
    auto check = [&](const char* src, float x, float y, float t, float expect, float tol = 1e-4f) {
        fx::Program p;
        if (!fx::compile(src, p)) {
            std::fprintf(f, "FAIL  %-34s  compile: %s\n", src, p.error.c_str());
            ++fails;
            return;
        }
        const float got = fx::eval(p, x, y, t, 1.0f, 2.0f, 3.0f);
        if (!(std::fabs(got - expect) <= tol)) {
            std::fprintf(f, "FAIL  %-34s  got %.6f  want %.6f\n", src, got, expect);
            ++fails;
        } else {
            std::fprintf(f, "ok    %-34s  %.6f\n", src, got);
        }
    };
    auto bad = [&](const char* src) {
        fx::Program p;
        if (fx::compile(src, p)) {
            std::fprintf(f, "FAIL  %-34s  should fail but passed\n", src);
            ++fails;
        } else {
            std::fprintf(f, "ok    %-34s  rejected: %s\n", src, p.error.c_str());
        }
    };

    check("1 + 2 * 3", 0, 0, 0, 7.0f);
    check("(1 + 2) * 3", 0, 0, 0, 9.0f);
    check("2 ^ 3 ^ 2", 0, 0, 0, 512.0f);          // 右结合
    check("-x^2", 3, 0, 0, -9.0f);                // 一元负号弱于 ^
    check("a * x + b", 2, 0, 0, 4.0f);            // a=1 b=2
    check("sin(pi/2)", 0, 0, 0, 1.0f);
    check("cos(0) + e^0", 0, 0, 0, 2.0f);
    check("hypot(3, 4)", 0, 0, 0, 5.0f);
    check("min(a, b) + max(a, b)", 0, 0, 0, 3.0f);
    check("mod(7, 4)", 0, 0, 0, 3.0f);
    check("t^2", 0, 0, 4, 16.0f);
    check("y*10", 0, 7, 0, 70.0f);
    check("sqrt(abs(-16))", 0, 0, 0, 4.0f);
    check("2^-2", 0, 0, 0, 0.25f);
    check("exp(log(5))", 0, 0, 0, 5.0f);
    check("sign(-3) + floor(2.9) + ceil(2.1)", 0, 0, 0, 4.0f);
    check("0.5*x + 1.5", 4, 0, 0, 3.5f);

    // ---- 省略乘号 / % / 绝对值 ----
    check("2x", 3, 0, 0, 6.0f);
    check("2(x+1)", 3, 0, 0, 8.0f);
    check("(x+1)(x-1)", 3, 0, 0, 8.0f);
    check("2sin(0)+1", 0, 0, 0, 1.0f);
    check("7 % 4", 0, 0, 0, 3.0f);
    check("|0-3| + |2|", 0, 0, 0, 5.0f);
    check("|x|*2", -3, 0, 0, 6.0f);

    // ---- 新增函数 ----
    check("sec(0)", 0, 0, 0, 1.0f);
    check("csc(pi/2)", 0, 0, 0, 1.0f);
    check("cot(pi/4)", 0, 0, 0, 1.0f);
    check("atan2(1, 1)", 0, 0, 0, kPi * 0.25f);
    check("log10(1000)", 0, 0, 0, 3.0f);
    check("log2(8)", 0, 0, 0, 3.0f);
    check("cbrt(27)", 0, 0, 0, 3.0f);
    check("round(2.6) + trunc(-2.9) + frac(2.25)", 0, 0, 0, 1.25f);
    check("clamp(9, 0, 4)", 0, 0, 0, 4.0f);
    check("lerp(2, 4, 0.25)", 0, 0, 0, 2.5f);
    check("smoothstep(0, 1, 0.5)", 0, 0, 0, 0.5f);
    check("step(0.5, 0.7) + step(0.5, 0.2)", 0, 0, 0, 1.0f);
    check("asinh(sinh(1))", 0, 0, 0, 1.0f);
    check("atanh(tanh(0.5))", 0, 0, 0, 0.5f);
    check("tau/2 - pi", 0, 0, 0, 0.0f, 1e-5f);

    // ---- 隐函数等式写法：F = G 当 F-G 用 ----
    check("x^2 + y^2 = 1", 0, 1, 0, 0.0f);       // 点 (0,1) 在圆上
    check("x^2 + y^2 = 1", 1, 0, 0, 0.0f);
    check("x^2 + y^2 = 1", 0, 2, 0, 3.0f);       // 4 - 1

    bad("");
    bad("1 +");
    bad("sin(");
    bad("foo(1)");
    bad("sin(1, 2)");
    bad("1 2");
    bad("(1+2");
    bad("|x");
    bad("clamp(1, 2)");
    bad("x = y = 1");

    std::fprintf(f, "\n%s (%d fails)\n", fails ? "FAILED" : "ALL PASS", fails);
    if (f != stdout) std::fclose(f);
    return fails ? 1 : 0;
}

int main() {
    // 自测模式：不开窗口，只跑表达式编译器并写报告
    if (const char* path = std::getenv("AINO_FN_SELFTEST")) return run_selftest(path);

    aino::WindowConfig cfg;
    cfg.width = 1180;
    cfg.height = 760;
    // 截图/回归用：AINO_FN_SIZE=960x640 直接指定初始尺寸
    if (const char* e = std::getenv("AINO_FN_SIZE")) {
        int w = 0, h = 0;
        if (std::sscanf(e, "%dx%d", &w, &h) == 2 && w > 200 && h > 200) {
            cfg.width = w;
            cfg.height = h;
        }
    }
    cfg.title = "aino · 函数显示器";
    cfg.min_width = 960;
    cfg.min_height = 640;
    cfg.center = true;

    aino::Window win(cfg);
    if (!win.ok()) return 1;

    UiState st;
    st.win = &win;
    if (const char* e = std::getenv("AINO_FN_CAT")) st.app.cat = std::atoi(e);
    if (const char* e = std::getenv("AINO_FN_SEL")) st.app.sel = std::atoi(e);
    if (const char* e = std::getenv("AINO_FN_ANIM")) st.app.anim = std::atoi(e) != 0;
    if (const char* e = std::getenv("AINO_FN_CUSTOM")) st.app.custom_src = e;
    if (const char* e = std::getenv("AINO_FN_CUSTOM2")) st.app.custom_src2 = e;
    if (const char* e = std::getenv("AINO_FN_MODE")) st.app.custom_mode = std::atoi(e);
    if (const char* e = std::getenv("AINO_FN_DARK")) st.app.dark = std::atoi(e) != 0;
    if (const char* e = std::getenv("AINO_FN_DERIV")) st.app.deriv = std::atoi(e) != 0;
    if (const char* e = std::getenv("AINO_FN_CONTOUR")) st.app.contour = std::atoi(e) != 0;
    if (st.app.dark) win.set_theme(aino::Theme::dark());
    st.refresh();
    st.reset_params();

    // 准星要跟着鼠标走，而指针在同一个"控件"里移动时库默认不重绘 —— 这里必须关掉
    win.set_hover_skip(false);

    win.run([&](aino::Ui& ui) {
        App& a = st.app;
        const aino::Theme& th = ui.theme();

        // 动画时钟：只在开启动画时推进（关掉后又是 0 帧 / 0% CPU）
        if (a.anim) {
            a.t += std::min(ui.frame_dt(), 0.05f);
            ui.set_animating(true);
        } else {
            ui.set_animating(false);
        }

        ui.clear(th.background);
        draw_sidebar(ui, st, th);

        const float W = ui.width(), H = ui.height();
        const float SIDE = 232.0f;
        const float x = SIDE + 22.0f;
        const float w = W - SIDE - 44.0f;
        const bool custom = (a.cat == kCustomCat);
        // 自定义区多一行表达式输入（36+10），参数方程再多一行
        const float bar_h = 106.0f + (custom ? 46.0f : 0.0f)
                                   + (custom && a.custom_mode == 1 ? 46.0f : 0.0f);
        const float gap = 12.0f;

        if (st.presets.empty() && !custom) st.refresh();
        const Preset* pre = nullptr;
        if (!custom && !st.presets.empty()) {
            a.sel = std::max(0, std::min(static_cast<int>(st.presets.size()) - 1, a.sel));
            pre = &st.presets[a.sel];
        }

        const Rect hdr(x, 22.0f, w, 62.0f);
        const Rect bar(x, H - 22.0f - bar_h, w, bar_h);
        const Rect card(x, hdr.bottom() + gap, w,
                        bar.y - gap - (hdr.bottom() + gap));

        // ---- 标题区 ----
        const std::string title = custom ? std::string("自定义公式") : pre->name;
        ui.heading(title, Rect(hdr.x, hdr.y, hdr.w * 0.62f, 26.0f),
                   Style().font(19.0f).left().middle());
        const std::string formula = custom
            ? (a.custom_mode == 1 ? ("x = " + a.custom_src + "    y = " + a.custom_src2)
                                  : a.custom_src)
            : pre->formula;
        const Color fcol = (!custom || a.prog.ok)
                               ? (th.background.luminance() < 0.5f ? Color::hex(0x60A5FA)
                                                                   : Color::hex(0x2563EB))
                               : th.destructive;
        ui.draw_text(formula, Rect(hdr.x, hdr.y + 28.0f, hdr.w * 0.66f, 20.0f), fcol,
                     Style().font(12.5f).left().middle());
        const Kind mode = custom ? (a.custom_mode == 0 ? Kind::Curve
                                  : a.custom_mode == 1 ? Kind::Param
                                  : a.custom_mode == 2 ? Kind::Polar
                                  : a.custom_mode == 3 ? Kind::Implicit : Kind::Field)
                                : pre->kind;
        ui.muted_text(kind_note(mode), Rect(hdr.x, hdr.y + 28.0f, hdr.w, 20.0f),
                      Style().font(11.5f).right().middle());
        ui.muted_text("拖拽平移 · 滚轮缩放 · 双击复位 · 空格暂停",
                      Rect(hdr.x, hdr.y + 48.0f, hdr.w, 16.0f),
                      Style().font(11.0f).right().middle());

        // ---- 绘图区 ----
        draw_plot(ui, st, card, th);

        // ---- 控制条 ----
        Preset fallback;
        fallback.name = "自定义公式";
        fallback.kind = mode;
        fallback.half_h = 3.0f;
        draw_controls(ui, st, bar, th, mode, pre ? *pre : fallback);
    });

    return 0;
}
