#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#include "stdlib.hpp"
#include "cw/lib/python_text.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <set>
#include <utility>

namespace cw::lib {
namespace {

constexpr std::uint32_t rotl32(std::uint32_t v, unsigned n) noexcept { return (v << n) | (v >> (32U - n)); }
constexpr std::uint32_t rotr32(std::uint32_t v, unsigned n) noexcept { return (v >> n) | (v << (32U - n)); }
std::uint32_t le32(const std::uint8_t* p) noexcept { return p[0] | (std::uint32_t(p[1]) << 8U) | (std::uint32_t(p[2]) << 16U) | (std::uint32_t(p[3]) << 24U); }
std::uint32_t be32(const std::uint8_t* p) noexcept { return (std::uint32_t(p[0]) << 24U) | (std::uint32_t(p[1]) << 16U) | (std::uint32_t(p[2]) << 8U) | p[3]; }
void put_le32(std::uint8_t* p, std::uint32_t v) noexcept { for (unsigned i=0;i<4;++i) p[i]=std::uint8_t(v>>(8U*i)); }
void put_be32(std::uint8_t* p, std::uint32_t v) noexcept { for (unsigned i=0;i<4;++i) p[i]=std::uint8_t(v>>(24U-8U*i)); }
void put_le64(std::uint8_t* p, std::uint64_t v) noexcept { for (unsigned i=0;i<8;++i) p[i]=std::uint8_t(v>>(8U*i)); }
void put_be64(std::uint8_t* p, std::uint64_t v) noexcept { for (unsigned i=0;i<8;++i) p[i]=std::uint8_t(v>>(56U-8U*i)); }
StdString hex_encode(const StdBytes& b) { static constexpr char h[]="0123456789abcdef"; StdString r; r.reserve(b.size()*2U); for(auto v:b){r+=h[v>>4U];r+=h[v&15U];} return r; }

} // namespace

StdlibError::StdlibError(StdlibErrorKind kind, const StdString& message) : std::runtime_error(message), m_kind(kind) {}

struct HashContext::Impl {
    HashAlgorithm algorithm;
    std::array<std::uint32_t,8> state{};
    std::array<std::uint64_t,8> blake_state{};
    std::uint64_t bit_count=0;
    std::uint64_t byte_count=0;
    std::size_t digest_size=0;
    StdBytes buffer;
    explicit Impl(HashAlgorithm a,std::size_t requested_size=0):algorithm(a){
        if(a==HashAlgorithm::MD5){state[0]=0x67452301U;state[1]=0xefcdab89U;state[2]=0x98badcfeU;state[3]=0x10325476U;}
        else if(a==HashAlgorithm::SHA1){state[0]=0x67452301U;state[1]=0xefcdab89U;state[2]=0x98badcfeU;state[3]=0x10325476U;state[4]=0xc3d2e1f0U;}
        else if(a==HashAlgorithm::SHA256) state={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
        else {
            digest_size=requested_size;
            if(digest_size<1||digest_size>64)throw StdlibError(StdlibErrorKind::InvalidArgument,"BLAKE2b digest_size must be 1..64");
            blake_state={0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
            blake_state[0]^=0x01010000ULL^static_cast<std::uint64_t>(digest_size);
        }
    }
    static std::uint64_t rotr64(std::uint64_t v,unsigned n)noexcept{return(v>>n)|(v<<(64U-n));}
    void process_blake(const std::uint8_t* block,bool last,std::uint64_t count){
        static constexpr std::uint64_t iv[8]={0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
        static constexpr unsigned sigma[12][16]={{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},{14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},{11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},{7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},{9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},{2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},{12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},{13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},{6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},{10,2,8,4,7,6,1,5,15,11,9,14,3,13,12,0},{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},{14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3}};
        std::uint64_t v[16],m[16];for(unsigned i=0;i<8;++i)v[i]=blake_state[i];for(unsigned i=0;i<8;++i)v[i+8]=iv[i];for(unsigned i=0;i<16;++i)m[i]=std::uint64_t(block[8*i])|(std::uint64_t(block[8*i+1])<<8U)|(std::uint64_t(block[8*i+2])<<16U)|(std::uint64_t(block[8*i+3])<<24U)|(std::uint64_t(block[8*i+4])<<32U)|(std::uint64_t(block[8*i+5])<<40U)|(std::uint64_t(block[8*i+6])<<48U)|(std::uint64_t(block[8*i+7])<<56U);v[12]^=count;v[13]^=0;if(last)v[14]=~v[14];
        auto G=[&](unsigned a,unsigned b,unsigned c,unsigned d,std::uint64_t x,std::uint64_t y){v[a]=v[a]+v[b]+x;v[d]=rotr64(v[d]^v[a],32);v[c]+=v[d];v[b]=rotr64(v[b]^v[c],24);v[a]=v[a]+v[b]+y;v[d]=rotr64(v[d]^v[a],16);v[c]+=v[d];v[b]=rotr64(v[b]^v[c],63);};
        for(unsigned r=0;r<12;++r){const auto*s=sigma[r];G(0,4,8,12,m[s[0]],m[s[1]]);G(1,5,9,13,m[s[2]],m[s[3]]);G(2,6,10,14,m[s[4]],m[s[5]]);G(3,7,11,15,m[s[6]],m[s[7]]);G(0,5,10,15,m[s[8]],m[s[9]]);G(1,6,11,12,m[s[10]],m[s[11]]);G(2,7,8,13,m[s[12]],m[s[13]]);G(3,4,9,14,m[s[14]],m[s[15]]);}
        for(unsigned i=0;i<8;++i)blake_state[i]^=v[i]^v[i+8];
    }
    void process(const std::uint8_t* block){
        if(algorithm==HashAlgorithm::MD5){
            static constexpr std::uint32_t k[64]={
                0xd76aa478U,0xe8c7b756U,0x242070dbU,0xc1bdceeeU,0xf57c0fafU,0x4787c62aU,0xa8304613U,0xfd469501U,0x698098d8U,0x8b44f7afU,0xffff5bb1U,0x895cd7beU,0x6b901122U,0xfd987193U,0xa679438eU,0x49b40821U,0xf61e2562U,0xc040b340U,0x265e5a51U,0xe9b6c7aaU,0xd62f105dU,0x02441453U,0xd8a1e681U,0xe7d3fbc8U,0x21e1cde6U,0xc33707d6U,0xf4d50d87U,0x455a14edU,0xa9e3e905U,0xfcefa3f8U,0x676f02d9U,0x8d2a4c8aU,0xfffa3942U,0x8771f681U,0x6d9d6122U,0xfde5380cU,0xa4beea44U,0x4bdecfa9U,0xf6bb4b60U,0xbebfbc70U,0x289b7ec6U,0xeaa127faU,0xd4ef3085U,0x04881d05U,0xd9d4d039U,0xe6db99e5U,0x1fa27cf8U,0xc4ac5665U,0xf4292244U,0x432aff97U,0xab9423a7U,0xfc93a039U,0x655b59c3U,0x8f0ccc92U,0xffeff47dU,0x85845dd1U,0x6fa87e4fU,0xfe2ce6e0U,0xa3014314U,0x4e0811a1U,0xf7537e82U,0xbd3af235U,0x2ad7d2bbU,0xeb86d391U};
            static constexpr unsigned s[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
            std::uint32_t a=state[0],b=state[1],c=state[2],d=state[3],x[16]; for(unsigned i=0;i<16;++i)x[i]=le32(block+4*i);
            for(unsigned i=0;i<64;++i){std::uint32_t f,g;if(i<16){f=(b&c)|(~b&d);g=i;}else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}else if(i<48){f=b^c^d;g=(3*i+5)%16;}else{f=c^(b|~d);g=(7*i)%16;}const auto old=d;d=c;c=b;b+=rotl32(a+f+k[i]+x[g],s[i]);a=old;}
            state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d; return;
        }
        if(algorithm==HashAlgorithm::SHA1){
            std::uint32_t w[80]{};for(unsigned i=0;i<16;++i)w[i]=be32(block+4*i);for(unsigned i=16;i<80;++i)w[i]=rotl32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);std::uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];for(unsigned i=0;i<80;++i){std::uint32_t f,k;if(i<20){f=(b&c)|(~b&d);k=0x5a827999U;}else if(i<40){f=b^c^d;k=0x6ed9eba1U;}else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcU;}else{f=b^c^d;k=0xca62c1d6U;}const auto t=rotl32(a,5)+f+e+k+w[i];e=d;d=c;c=rotl32(b,30);b=a;a=t;}state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;return;
        }
        static constexpr std::uint32_t k[64]={0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
        std::uint32_t w[64]{};for(unsigned i=0;i<16;++i)w[i]=be32(block+4*i);for(unsigned i=16;i<64;++i){const auto s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3),s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}std::uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];for(unsigned i=0;i<64;++i){const auto s1=rotr32(e,6)^rotr32(e,11)^rotr32(e,25),ch=(e&f)^(~e&g),t1=h+s1+ch+k[i]+w[i],s0=rotr32(a,2)^rotr32(a,13)^rotr32(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(const std::uint8_t* data,std::size_t size){if(!size)return;if(algorithm==HashAlgorithm::BLAKE2B){while(size){const auto take=std::min<std::size_t>(128-buffer.size(),size);buffer.insert(buffer.end(),data,data+take);data+=take;size-=take;if(buffer.size()==128&&size>0){byte_count+=128;process_blake(buffer.data(),false,byte_count);buffer.clear();}}return;}bit_count+=std::uint64_t(size)*8U;if(!buffer.empty()){const auto take=std::min<std::size_t>(64-buffer.size(),size);buffer.insert(buffer.end(),data,data+take);data+=take;size-=take;if(buffer.size()==64){process(buffer.data());buffer.clear();}}while(size>=64){process(data);data+=64;size-=64;}if(size)buffer.insert(buffer.end(),data,data+size);}
    StdBytes finish()const{Impl copy=*this;if(copy.algorithm==HashAlgorithm::BLAKE2B){const auto final_size=copy.buffer.size();copy.buffer.resize(128,0);copy.byte_count+=final_size;copy.process_blake(copy.buffer.data(),true,copy.byte_count);StdBytes out(copy.digest_size);for(std::size_t i=0;i<out.size();++i)out[i]=static_cast<std::uint8_t>(copy.blake_state[i/8]>>(8U*(i%8)));return out;}copy.buffer.push_back(0x80);while(copy.buffer.size()%64!=56)copy.buffer.push_back(0);std::array<std::uint8_t,8> len{};if(copy.algorithm==HashAlgorithm::MD5)put_le64(len.data(),copy.bit_count);else put_be64(len.data(),copy.bit_count);copy.buffer.insert(copy.buffer.end(),len.begin(),len.end());for(std::size_t i=0;i<copy.buffer.size();i+=64)copy.process(copy.buffer.data()+i);const std::size_t words=copy.algorithm==HashAlgorithm::MD5?4:(copy.algorithm==HashAlgorithm::SHA1?5:8);StdBytes out(words*4);for(std::size_t i=0;i<words;++i)if(copy.algorithm==HashAlgorithm::MD5)put_le32(out.data()+4*i,copy.state[i]);else put_be32(out.data()+4*i,copy.state[i]);return out;}
};

HashContext::HashContext():m_impl(std::make_unique<Impl>(HashAlgorithm::MD5)){}
HashContext::HashContext(HashAlgorithm a,std::size_t digest_size):m_impl(std::make_unique<Impl>(a,digest_size)){}
HashContext::~HashContext()=default; HashContext::HashContext(HashContext&&)noexcept=default; HashContext& HashContext::operator=(HashContext&&)noexcept=default;
HashContext HashContext::md5(){return HashContext(HashAlgorithm::MD5);} HashContext HashContext::sha1(){return HashContext(HashAlgorithm::SHA1);} HashContext HashContext::sha256(){return HashContext(HashAlgorithm::SHA256);}
bool HashContext::blake2b_self_test(){
    static constexpr char empty512[]="786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce";
    static constexpr char abc512[]="ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923";
    static constexpr char empty128[]="cae66941d9efbd404e4d88758ea67670";
    static constexpr char abc128[]="cf4ab791c62b8d2b2109c90275287816";
    const std::uint8_t abc[]={'a','b','c'};
    HashContext empty(HashAlgorithm::BLAKE2B,64);
    HashContext abc_hash(HashAlgorithm::BLAKE2B,64); abc_hash.update(abc,sizeof(abc));
    HashContext empty16(HashAlgorithm::BLAKE2B,16);
    HashContext abc16(HashAlgorithm::BLAKE2B,16); abc16.update(abc,sizeof(abc));
    return empty.hexdigest()==empty512 && abc_hash.hexdigest()==abc512 &&
           empty16.hexdigest()==empty128 && abc16.hexdigest()==abc128;
}
HashContext HashContext::blake2b(std::size_t digest_size){
    static std::once_flag checked;
    std::call_once(checked, [](){
        if(!HashContext::blake2b_self_test())
            throw StdlibError(StdlibErrorKind::Parse,"BLAKE2b self-test failed");
    });
    return HashContext(HashAlgorithm::BLAKE2B,digest_size);
}
void HashContext::update(const void* data,std::size_t size){if(!m_impl||(data==nullptr&&size))throw StdlibError(StdlibErrorKind::InvalidArgument,"invalid hash input");m_impl->update(static_cast<const std::uint8_t*>(data),size);}
StdBytes HashContext::digest()const{if(!m_impl)throw StdlibError(StdlibErrorKind::InvalidArgument,"empty hash context");return m_impl->finish();} StdString HashContext::hexdigest()const{return hex_encode(digest());}
StdBytes hash_bytes(HashAlgorithm a,const StdBytes&d,std::size_t digest_size){HashContext c=a==HashAlgorithm::BLAKE2B?HashContext::blake2b(digest_size):HashContext(a,digest_size);c.update(d);return c.digest();} StdString hash_hex(HashAlgorithm a,const StdBytes&d,std::size_t digest_size){return hex_encode(hash_bytes(a,d,digest_size));}
StdBytes hash_file(HashAlgorithm a,const StdString&path,std::size_t chunk,std::size_t digest_size){if(path.empty()||chunk==0)throw StdlibError(StdlibErrorKind::InvalidArgument,"invalid hash file arguments");std::ifstream f(std::filesystem::u8path(path),std::ios::binary);if(!f)throw StdlibError(StdlibErrorKind::Io,"cannot open hash file");HashContext c=a==HashAlgorithm::BLAKE2B?HashContext::blake2b(digest_size):HashContext(a,digest_size);StdBytes b(chunk);while(f){f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));const auto n=f.gcount();if(n>0)c.update(b.data(),static_cast<std::size_t>(n));}if(!f.eof())throw StdlibError(StdlibErrorKind::Io,"error reading hash file");return c.digest();} StdString hash_file_hex(HashAlgorithm a,const StdString&p,std::size_t c,std::size_t digest_size){return hex_encode(hash_file(a,p,c,digest_size));}
// 以下に Base64 と色の関数を定義する。
StdString base64_encode(const StdBytes& data, bool url_safe, bool padding) {
    const char* alphabet = url_safe ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    StdString result; result.reserve(((data.size()+2U)/3U)*4U);
    for (std::size_t i=0;i<data.size();i+=3U) { const std::size_t n=std::min<std::size_t>(3U,data.size()-i); const std::uint32_t v=(std::uint32_t(data[i])<<16U)|(n>1U?std::uint32_t(data[i+1U])<<8U:0U)|(n>2U?std::uint32_t(data[i+2U]):0U); result.push_back(alphabet[(v>>18U)&63U]); result.push_back(alphabet[(v>>12U)&63U]); result.push_back(n>1U?alphabet[(v>>6U)&63U]:'='); result.push_back(n>2U?alphabet[v&63U]:'='); }
    if(!padding) while(!result.empty()&&result.back()=='=') result.pop_back(); return result;
}
StdBytes base64_decode(const StdString& text,bool url_safe,bool strict) {
    if(strict&&text.size()%4U!=0U) throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 length");
    auto val=[url_safe](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(url_safe&&c=='-')return 62;if(url_safe&&c=='_')return 63;if(!url_safe&&c=='+')return 62;if(!url_safe&&c=='/')return 63;return -1;};
    StdString source;
    source.reserve(text.size());
    for(char c:text){
        if(!strict&&(c==' '||c=='\t'||c=='\r'||c=='\n'))continue;
        source.push_back(c);
    }
    if(strict&&source.size()%4U!=0U)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 length");
    std::size_t pads=0;
    while(pads<source.size()&&source[source.size()-1U-pads]=='=')++pads;
    if(pads>2U)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 padding");
    const auto first_pad=source.find('=');
    if(first_pad!=StdString::npos&&first_pad+ pads!=source.size())throw StdlibError(StdlibErrorKind::InvalidEncoding,"data after base64 padding");
    if(strict&&pads>0U&&source.size()%4U!=0U)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 padding");
    const std::size_t data_size=source.size()-pads;
    if(data_size%4U==1U)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 quantum");
    StdBytes out;std::uint32_t acc=0;unsigned bits=0;
    for(std::size_t i=0;i<data_size;++i){const int v=val(source[i]);if(v<0)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 character");acc=(acc<<6U)|std::uint32_t(v);bits+=6U;if(bits>=8U){bits-=8U;out.push_back(std::uint8_t((acc>>bits)&255U));}}
    if((bits!=0U&&(acc&((std::uint32_t{1}<<bits)-1U))!=0U)||
       (strict&&((pads==1U&&data_size%4U!=3U)||(pads==2U&&data_size%4U!=2U))))
        throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid base64 quantum");
    return out;
}
ColorTriple rgb_to_hsv(const ColorTriple& r){const double mx=std::max({r[0],r[1],r[2]}),mn=std::min({r[0],r[1],r[2]}),d=mx-mn;if(mx==0)return {0,0,mx};const double s=d/mx;if(d==0)return {0,s,mx};double h=r[0]==mx?(r[1]-r[2])/d:(r[1]==mx?2+(r[2]-r[0])/d:4+(r[0]-r[1])/d);h/=6;if(h<0)h+=1;return {h,s,mx};}
ColorTriple hsv_to_rgb(const ColorTriple& x){const double h=x[0],s=x[1],v=x[2];if(s==0)return {v,v,v};const double i=std::floor(h*6),f=h*6-i,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));switch(static_cast<int>(i)%6){case 0:return {v,t,p};case 1:return {q,v,p};case 2:return {p,v,t};case 3:return {p,q,v};case 4:return {t,p,v};default:return {v,p,q};}}
ColorTriple rgb_to_hls(const ColorTriple& r){const double mx=std::max({r[0],r[1],r[2]}),mn=std::min({r[0],r[1],r[2]}),l=(mx+mn)/2,d=mx-mn;if(d==0)return {0,l,0};const double s=l<=.5?d/(mx+mn):d/(2-mx-mn);double h=r[0]==mx?(r[1]-r[2])/d:(r[1]==mx?2+(r[2]-r[0])/d:4+(r[0]-r[1])/d);h/=6;if(h<0)h+=1;return {h,l,s};}
ColorTriple hls_to_rgb(const ColorTriple& x){const double h=x[0],l=x[1],s=x[2];if(s==0)return {l,l,l};const double m2=l<=.5?l*(1+s):l+s-l*s,m1=2*l-m2;auto hue=[&](double q){if(q<0)q+=1;if(q>1)q-=1;if(q*6<1)return m1+(m2-m1)*q*6;if(q*2<1)return m2;if(q*3<2)return m1+(m2-m1)*(2.0/3-q)*6;return m1;};return {hue(h+1.0/3),hue(h),hue(h-1.0/3)};}
ColorTriple rgb_to_yiq(const ColorTriple& r){return {.299*r[0]+.587*r[1]+.114*r[2],.596*r[0]-.274*r[1]-.322*r[2],.211*r[0]-.523*r[1]+.312*r[2]};}
ColorTriple yiq_to_rgb(const ColorTriple& y){return {std::clamp(y[0]+.956*y[1]+.621*y[2],0.0,1.0),std::clamp(y[0]-.272*y[1]-.647*y[2],0.0,1.0),std::clamp(y[0]-1.106*y[1]+1.703*y[2],0.0,1.0)};}
namespace {
std::vector<std::uint32_t> utf8_decode(const StdString& text){std::vector<std::uint32_t> r;for(std::size_t i=0;i<text.size();){const auto c=static_cast<unsigned char>(text[i]);std::uint32_t cp=0;unsigned n=0;if(c<0x80){cp=c;n=1;}else if((c&0xe0)==0xc0){cp=c&31;n=2;}else if((c&0xf0)==0xe0){cp=c&15;n=3;}else if((c&0xf8)==0xf0){cp=c&7;n=4;}else throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid UTF-8");if(i+n>text.size())throw StdlibError(StdlibErrorKind::InvalidEncoding,"truncated UTF-8");for(unsigned j=1;j<n;++j){const auto d=static_cast<unsigned char>(text[i+j]);if((d&0xc0)!=0x80)throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid UTF-8");cp=(cp<<6)|(d&63);}if((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw StdlibError(StdlibErrorKind::InvalidEncoding,"invalid UTF-8");r.push_back(cp);i+=n;}return r;}
struct GlobToken{enum Kind{Literal,Any,Star,Class} kind;std::uint32_t literal{};bool negate{};std::vector<std::pair<std::uint32_t,std::uint32_t>> ranges;};
std::vector<GlobToken> glob_tokens(const std::vector<std::uint32_t>& p){std::vector<GlobToken>o;for(std::size_t i=0;i<p.size();){if(p[i]=='*'){if(o.empty()||o.back().kind!=GlobToken::Star)o.push_back({GlobToken::Star});++i;continue;}if(p[i]=='?'){o.push_back({GlobToken::Any});++i;continue;}if(p[i]=='['){std::size_t j=i+1;bool neg=false;if(j<p.size()&&p[j]=='!'){neg=true;++j;}std::vector<std::pair<std::uint32_t,std::uint32_t>> ranges;if(j<p.size()&&p[j]==']'){ranges.push_back({']',']'});++j;}for(;j<p.size()&&p[j]!=']';++j){const auto a=p[j];if(j+2<p.size()&&p[j+1]=='-'&&p[j+2]!=']'){ranges.push_back({a,p[j+2]});j+=2;}else ranges.push_back({a,a});}if(j<p.size()&&p[j]==']'&&!ranges.empty()){o.push_back({GlobToken::Class,0,neg,std::move(ranges)});i=j+1;continue;}}o.push_back({GlobToken::Literal,p[i]});++i;}return o;}
bool class_match(const GlobToken&t,std::uint32_t c){bool found=false;for(const auto&r:t.ranges)if(c>=r.first&&c<=r.second){found=true;break;}return t.negate?!found:found;}
StdString trim_ascii(const StdString&v){const auto space=[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\v';};std::size_t b=0,e=v.size();while(b<e&&space(static_cast<unsigned char>(v[b])))++b;while(e>b&&space(static_cast<unsigned char>(v[e-1])))--e;return v.substr(b,e-b);}
StdString lower_ascii(const StdString&v){StdString r=v;for(char&c:r)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return r;}
}

bool fnmatchcase(const StdString& name,const StdString& pattern){const auto n=utf8_decode(name);const auto p=glob_tokens(utf8_decode(pattern));std::map<std::pair<std::size_t,std::size_t>,bool> memo;std::function<bool(std::size_t,std::size_t)>m=[&](std::size_t i,std::size_t j){const auto key=std::make_pair(i,j);if(auto it=memo.find(key);it!=memo.end())return it->second;bool ok;if(j==p.size())ok=i==n.size();else if(p[j].kind==GlobToken::Star)ok=m(i,j+1)||(i<n.size()&&m(i+1,j));else ok=i<n.size()&&((p[j].kind==GlobToken::Any)||(p[j].kind==GlobToken::Literal&&p[j].literal==n[i])||(p[j].kind==GlobToken::Class&&class_match(p[j],n[i])))&&m(i+1,j+1);memo[key]=ok;return ok;};return m(0,0);}
bool fnmatch(const StdString& name,const StdString& pattern){
#if defined(_WIN32)
    return fnmatchcase(lower_ascii(name), lower_ascii(pattern));
#else
    return fnmatchcase(name,pattern);
#endif
}
StdString fnmatch_translate(const StdString& pattern){const auto p=utf8_decode(pattern);StdString r="(?s:";for(std::size_t i=0;i<p.size();++i){const auto c=p[i];if(c=='*')r+=".*";else if(c=='?')r+='.';else if(c=='['){std::size_t j=i+1;bool neg=false;if(j<p.size()&&p[j]=='!'){neg=true;++j;}if(neg)r+="[^";else r+='[';if(j<p.size()&&p[j]==']'){r+="\\]";++j;}for(;j<p.size()&&p[j]!=']';++j){const auto q=p[j];if(StdString("\\\\^-").find(char(q))!=StdString::npos)r+='\\';r+=char(q);}if(j<p.size()){r+=']';i=j;}else r+="\\[";}else if(StdString(".\\\\+()^$|{}").find(char(c))!=StdString::npos){r+='\\';r+=char(c);}else r+=char(c);}r+=")\\Z";return r;}

ConfigParser::ConfigParser(const Values& defaults):m_defaults(defaults){Values n;for(const auto&e:m_defaults)n[lower_ascii(e.first)]=e.second;m_defaults=std::move(n);}
void ConfigParser::clear(){m_sections.clear();}
bool ConfigParser::read(const StdString& path)
{
    const auto native = std::filesystem::u8path(path);
    StdString text;
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        // マウント済み論理パスのファイル基本処理には Storage
        // を使う。 解析・既定値・補間・検証は共通のままにする。
        const auto bytes = ::cardwirth::platform::read_mounted_user_file(native);
        if (!bytes) return false;
        text.reserve(bytes->size());
        for (const auto byte : *bytes) text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    } else {
        std::ifstream input(native, std::ios::binary);
        if (!input) return false;
        std::ostringstream buffer; buffer << input.rdbuf();
        if (!input.good() && !input.eof()) throw StdlibError(StdlibErrorKind::Io, "error reading config file");
        text = buffer.str();
    }
    const Bytes bytes(text.begin(), text.end());
    text = ::cw::lib::encoding::decode_system_text(bytes);
    // Py の open() と同様、ConfigParser が行を読む前に CRLF と単独の CR
    // を変換する。 read_string() はデコード済み文字列を受け取るため、その変換は行わない。
    StdString normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\r') normalized.push_back(text[i]);
        else {
            normalized.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        }
    }
    read_string(normalized, path);
    return true;
}
void ConfigParser::read_string(const StdString& input, const StdString& source)
{
    // SafeConfigParser の既定文法に合わせ、セクション名は大文字小文字を区
    // 別し、項目名は小文字化し、DEFAULT を継承する。BOM は除かない。 mode.ini
    // の呼び出し側と標準ライブラリー利用側で、この一つのパーサーを共有する。
    (void)utf8_decode(input);
    std::istringstream stream(input);
    StdString line, section, option;
    Values* current = nullptr;
    std::size_t indentation = 0;
    std::set<StdString> added_sections;
    std::set<std::pair<StdString, StdString>> added_options;
    bool malformed = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto value = python_strip(line);
        if (!value.empty() && (value.front() == '#' || value.front() == ';')) continue;
        if (value.empty()) {
            if (current && !option.empty()) (*current)[option] += '\n';
            continue;
        }
        const auto chars = utf8_decode(line);
        std::size_t indent = 0;
        while (indent < chars.size() && is_python_str_whitespace(chars[indent])) ++indent;
        if (current && !option.empty() && indent > indentation) {
            (*current)[option] += '\n' + value;
            continue;
        }
        indentation = indent;
        // Reboot の Python 3.8 は (?P<header>[^]]+)
        // を使い、最初の「]」でセクション名を終え、後続の文字は無視する。 後の
        // CPython の貪欲なパターンに変えず、元の実行環境の文法を維持する。
        const auto close = value.find(']');
        if (value.front() == '[' && close != StdString::npos && close > 1) {
            section = value.substr(1, close - 1);
            if (section == "DEFAULT") current = &m_defaults;
            else {
                if (!added_sections.insert(section).second)
                    throw StdlibError(StdlibErrorKind::Duplicate, source + ": duplicate section");
                current = &m_sections[section];
            }
            option.clear();
            continue;
        }
        if (!current) throw StdlibError(StdlibErrorKind::Parse, source + ": option before section");
        const auto delimiter = value.find_first_of("=:");
        if (delimiter == StdString::npos) { malformed = true; continue; }
        option = ::cw::lib::encoding::python_lower(python_rstrip(value.substr(0, delimiter)));
        if (option.empty()) malformed = true;
        if (!added_options.emplace(section, option).second)
            throw StdlibError(StdlibErrorKind::Duplicate, source + ": duplicate option");
        (*current)[option] = python_strip(value.substr(delimiter + 1));
    }
    // CPython と同様、蓄積した構文エラーを通知する前に、全複数
    // 行値を結合して末尾空白を除く。 不正な行を黙って受け入れない。
    for (auto& [key, value] : m_defaults) value = python_rstrip(value);
    for (auto& [name, values] : m_sections)
        for (auto& [key, value] : values) value = python_rstrip(value);
    if (malformed) throw StdlibError(StdlibErrorKind::Parse, source + ": malformed option");
}
bool ConfigParser::has_section(const StdString& section)const{return m_sections.find(section)!=m_sections.end();}
bool ConfigParser::has_option(const StdString& section,const StdString& option)const{auto i=m_sections.find(section);if(i==m_sections.end()&&section!="DEFAULT")return false;const auto k=::cw::lib::encoding::python_lower(option);return (i!=m_sections.end()&&i->second.find(k)!=i->second.end())||m_defaults.find(k)!=m_defaults.end();}
std::vector<StdString> ConfigParser::sections()const{std::vector<StdString>r;for(const auto&e:m_sections)r.push_back(e.first);return r;}
ConfigParser::Values ConfigParser::items(const StdString& section)const{auto i=m_sections.find(section);if(i==m_sections.end()&&section!="DEFAULT")throw StdlibError(StdlibErrorKind::Missing,"missing config section");Values r=m_defaults;if(i!=m_sections.end())for(const auto&e:i->second)r[e.first]=e.second;for(auto&[key,value]:r)value=get_required(section,key);return r;}
StdString ConfigParser::get(const StdString& section, const StdString& option, const StdString& fallback) const
{
    const auto it = m_sections.find(section);
    if (section != "DEFAULT" && it == m_sections.end()) return fallback;
    Values values = m_defaults;
    if (it != m_sections.end()) for (const auto& [key, value] : it->second) values[key] = value;
    const auto found = values.find(::cw::lib::encoding::python_lower(option));
    if (found == values.end()) return fallback;
    std::function<StdString(const StdString&, unsigned)> expand;
    expand = [&](const StdString& raw, unsigned depth) -> StdString {
        if (depth > 10) throw StdlibError(StdlibErrorKind::Parse, "config interpolation depth exceeded");
        StdString result;
        for (std::size_t pos = 0; pos < raw.size();) {
            const auto percent = raw.find('%', pos);
            if (percent == StdString::npos) { result += raw.substr(pos); break; }
            result += raw.substr(pos, percent - pos);
            if (percent + 1 < raw.size() && raw[percent + 1] == '%') {
                result += '%'; pos = percent + 2; continue;
            }
            const auto close = raw.find(')', percent + 2);
            if (percent + 1 >= raw.size() || raw[percent + 1] != '(' ||
                close == StdString::npos || close <= percent + 2 ||
                close + 1 >= raw.size() || raw[close + 1] != 's')
                throw StdlibError(StdlibErrorKind::Parse, "invalid config interpolation");
            const auto key = ::cw::lib::encoding::python_lower(raw.substr(percent + 2, close - percent - 2));
            const auto value = values.find(key);
            if (value == values.end()) throw StdlibError(StdlibErrorKind::Missing, "missing config interpolation option");
            result += value->second.find('%') == StdString::npos ? value->second : expand(value->second, depth + 1);
            pos = close + 2;
        }
        return result;
    };
    return expand(found->second, 1);
}
StdString ConfigParser::get_required(const StdString& section,const StdString& option)const{if(!has_option(section,option))throw StdlibError(StdlibErrorKind::Missing,"missing config option");return get(section,option);}
void ConfigParser::set(const StdString& section,const StdString& option,const StdString& value){auto i=m_sections.find(section);if(i==m_sections.end())throw StdlibError(StdlibErrorKind::Missing,"missing config section");i->second[lower_ascii(option)]=value;}

} // namespace cw::lib
