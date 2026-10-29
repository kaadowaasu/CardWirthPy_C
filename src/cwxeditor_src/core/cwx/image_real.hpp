#pragma once
#include <bit>
#include <cstdint>
#include <type_traits>

namespace cwxeditor::cwx::graphics::detail {
// Dの再サンプリングは仮数64ビットのx87 realを使う。MSVCの
// long doubleは53ビットしかなく、 通常の文字のアルファ値に
// も差が出る（5→3の縮小で169と170）。整数の仮数と二つの64ビッ
// ト領域で、 全環境で同じ最近接・偶数丸めを行う。浮動小数の残差和・epsilon
// ・拡張レジスター・OS別規則には頼らない。 対象は有限の画像計算
// に限り、一般的なDのrealの指数やNaNを扱うAPIではない。
class ImageReal {
    struct Wide {std::uint64_t high{},low{};};
    std::uint64_t mantissa_{};
    int exponent_{}; // 値＝仮数×2の（指数−63）乗。
    bool negative_{};
    static Wide right(Wide value,unsigned count) noexcept {
        if(!count)return value;
        if(count>=128)return {0,(value.high||value.low)?1U:0U};
        if(count>=64){const auto lost=value.low || (count>64 && (value.high<<(128-count)));return {0,(value.high>>(count-64))|std::uint64_t(lost)};}
        const auto lost=value.low<<(64-count);
        return {value.high>>count,(value.low>>count)|(value.high<<(64-count))|std::uint64_t(lost!=0)};
    }
    static Wide left(Wide value,unsigned count) noexcept {
        if(!count)return value;
        if(count>=64)return {value.low<<(count-64),0};
        return {(value.high<<count)|(value.low>>(64-count)),value.low<<count};
    }
    static ImageReal rounded(Wide value,int exponent,bool negative) noexcept {
        if(!value.high&&!value.low)return {};
        const auto shift=value.high ? std::countl_zero(value.high) : 64+std::countl_zero(value.low);
        value=left(value,unsigned(shift));exponent-=shift;
        const auto up=value.low>0x8000000000000000ULL || (value.low==0x8000000000000000ULL && (value.high&1));
        if(up && ++value.high==0){value.high=0x8000000000000000ULL;++exponent;}
        ImageReal result;result.mantissa_=value.high;result.exponent_=exponent;result.negative_=negative;return result;
    }
    static bool smaller(ImageReal a,ImageReal b) noexcept {
        if(!a.mantissa_)return b.mantissa_!=0;
        if(!b.mantissa_)return false;
        return a.exponent_!=b.exponent_ ? a.exponent_<b.exponent_ : a.mantissa_<b.mantissa_;
    }
    static Wide multiply(std::uint64_t a,std::uint64_t b) noexcept {
        const std::uint64_t a0=a&0xffffffffULL,a1=a>>32,b0=b&0xffffffffULL,b1=b>>32;
        const auto first=a0*b0;
        const auto middle=a1*b0+(first>>32);
        const auto last=a0*b1+(middle&0xffffffffULL);
        return {a1*b1+(middle>>32)+(last>>32),(last<<32)|(first&0xffffffffULL)};
    }
public:
    ImageReal()=default;
    template<class T> requires std::is_integral_v<T>
    ImageReal(T integer) noexcept {
        std::uint64_t magnitude;
        if constexpr(std::is_signed_v<T>){negative_=integer<0;magnitude=negative_?std::uint64_t(-(integer+1))+1:std::uint64_t(integer);}
        else magnitude=std::uint64_t(integer);
        if(magnitude){exponent_=63-std::countl_zero(magnitude);mantissa_=magnitude<<(63-exponent_);}
    }
    friend ImageReal operator+(ImageReal a,ImageReal b) noexcept {
        if(!a.mantissa_)return b;if(!b.mantissa_)return a;
        if(smaller(a,b)){const auto temporary=a;a=b;b=temporary;}
        auto x=Wide{a.mantissa_,0},y=right({b.mantissa_,0},unsigned(a.exponent_-b.exponent_));
        if(a.negative_!=b.negative_){
            const auto borrow=x.low<y.low;
            return rounded({x.high-y.high-borrow,x.low-y.low},a.exponent_,a.negative_);
        }
        const auto low=x.low+y.low;
        const auto carry=low<x.low;
        auto high=x.high+y.high;
        const auto overflow=high<x.high || (carry && high==UINT64_MAX);
        high+=carry;
        int exponent=a.exponent_;
        if(overflow){const auto lost=low&1;x={(high>>1)|0x8000000000000000ULL,(low>>1)|(high<<63)|lost};++exponent;}
        else x={high,low};
        return rounded(x,exponent,a.negative_);
    }
    friend ImageReal operator-(ImageReal a,ImageReal b) noexcept {b.negative_=!b.negative_;return a+b;}
    friend ImageReal operator*(ImageReal a,ImageReal b) noexcept {
        if(!a.mantissa_||!b.mantissa_)return {};
        return rounded(multiply(a.mantissa_,b.mantissa_),a.exponent_+b.exponent_+1,a.negative_!=b.negative_);
    }
    friend ImageReal operator/(ImageReal a,ImageReal b) noexcept {
        if(!a.mantissa_||!b.mantissa_)return {};
        auto exponent=a.exponent_-b.exponent_;
        Wide numerator;
        if(a.mantissa_>=b.mantissa_)numerator={a.mantissa_>>1,a.mantissa_<<63};
        else{numerator={a.mantissa_,0};--exponent;}
        std::uint64_t quotient{},remainder{};
        for(int bit=127;bit>=0;--bit){
            const auto overflow=remainder>>63;
            const auto digit=bit>=64 ? (numerator.high>>(bit-64))&1 : (numerator.low>>bit)&1;
            remainder=(remainder<<1)|digit;
            if(overflow || remainder>=b.mantissa_){remainder-=b.mantissa_;if(bit<64)quotient|=std::uint64_t(1)<<bit;}
        }
        const auto distance=b.mantissa_-remainder;
        if(remainder>distance || (remainder==distance && (quotient&1)))
            if(++quotient==0){quotient=0x8000000000000000ULL;++exponent;}
        ImageReal result;result.mantissa_=quotient;result.exponent_=exponent;result.negative_=a.negative_!=b.negative_;return result;
    }
    template<class T> requires std::is_integral_v<T>
    explicit operator T() const noexcept {
        if(!mantissa_)return 0;
        // DMDのx64のrealからint32への変換は、まずdoubleへ
        // 丸めてから切り捨てる。 元の実行結果の254.999999999999999986
        // というrealが255になるのもこのため。 前段の64ビット演算
        // とともに、この変換境界をOSによらず保つ。 long/ulong
        // への変換はrealから直接切り捨てるため、同じ値はlongの254
        // となり、 1よりわずかに小さいrealはulongの0になる。
        auto exponent=exponent_;
        auto mantissa=mantissa_;
        if constexpr(sizeof(T)<=4){
            auto significand=mantissa>>11;
            const auto remainder=mantissa&0x7ff;
            if(remainder>0x400 || (remainder==0x400 && (significand&1)))++significand;
            if(significand==0x20000000000000ULL){significand>>=1;++exponent;}
            mantissa=significand<<11;
        }
        if(exponent<0)return 0;
        const auto integer=exponent<=63 ? mantissa>>(63-exponent) : mantissa<<(exponent-63);
        return static_cast<T>(negative_ ? std::uint64_t(0)-integer : integer);
    }
};
} // namespace cwxeditor::cwx::graphics::detail
