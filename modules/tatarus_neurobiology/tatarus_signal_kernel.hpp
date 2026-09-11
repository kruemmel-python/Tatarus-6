#pragma once
#include <algorithm>
#include <cmath>

namespace tatarus_signal_kernel {
inline double sanitize(double v){ return std::isfinite(v) ? std::clamp(v,-1000000.0,1000000.0) : 0.0; }
inline double ag_expclamp(double x){ return std::exp(std::clamp(sanitize(x),-20.0,20.0)); }
inline double ag_logabs(double x){ return std::log(std::abs(sanitize(x))+1e-9); }
inline double ag_sdiv(double a,double b){ return sanitize(a)/(std::abs(sanitize(b))+1e-6); }
inline double kernel(double x){ x=sanitize(x); return sanitize(((ag_logabs(std::sin(std::cos(ag_sdiv(x,x)))) - std::tanh(ag_sdiv((std::sin(ag_sdiv(x,x)) * -0.357064),(ag_logabs(std::sin(std::cos(ag_sdiv(x,x)))) - ag_logabs(std::sin(std::cos(ag_sdiv(x,x)))))))) - std::sin((ag_logabs(std::sin(std::cos(ag_sdiv(x,x)))) * (std::sin(std::cos(ag_sdiv(x,x))) + std::cos(std::sin(ag_sdiv(x,x)))))))); }
}
