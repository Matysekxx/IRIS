#ifndef HASHLIB_H
#define HASHLIB_H

#include "core/Native.h"
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace iris::std_lib {

    static inline std::string to_hex(const std::vector<uint8_t>& bytes) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : bytes) ss << std::setw(2) << (int)b;
        return ss.str();
    }

    // ---------- MD5 ----------
    static inline uint32_t md5_leftrotate(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

    static inline std::vector<uint8_t> md5_bytes(const std::string& s) {
        std::vector<uint8_t> msg(s.begin(), s.end());
        uint64_t origLen = msg.size();
        msg.push_back(0x80);
        while (msg.size() % 64 != 56) msg.push_back(0x00);
        for (int i = 0; i < 8; i++) msg.push_back((uint8_t)((origLen * 8) >> (8 * i)));

        uint32_t sh[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
        uint32_t K[64] = {0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
        uint32_t a0=0x67452301,b0=0xefcdab89,c0=0x98badcfe,d0=0x10325476;

        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t M[16];
            for (int j = 0; j < 16; j++)
                M[j] = (uint32_t)msg[i+j*4] | ((uint32_t)msg[i+j*4+1]<<8) | ((uint32_t)msg[i+j*4+2]<<16) | ((uint32_t)msg[i+j*4+3]<<24);
            uint32_t A=a0,B=b0,C=c0,D=d0;
            for (int j = 0; j < 64; j++) {
                uint32_t F; int g;
                if (j < 16) { F=(B&C)|((~B)&D); g=j; }
                else if (j < 32) { F=(D&B)|((~D)&C); g=(5*j+1)%16; }
                else if (j < 48) { F=B^C^D; g=(3*j+5)%16; }
                else { F=C^(B|(~D)); g=(7*j)%16; }
                F = F + A + K[j] + M[g];
                A = D; D = C; C = B; B = B + md5_leftrotate(F, sh[j]);
            }
            a0+=A; b0+=B; c0+=C; d0+=D;
        }
        std::vector<uint8_t> out(16);
        uint32_t vals[4] = {a0,b0,c0,d0};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                out[i*4+j] = (uint8_t)((vals[i] >> (8*j)) & 0xFF);
        return out;
    }

    // ---------- SHA-1 ----------
    static inline uint32_t sha1_rotl(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

    static inline std::vector<uint8_t> sha1_bytes(const std::string& s) {
        std::vector<uint8_t> msg(s.begin(), s.end());
        uint64_t origLen = msg.size();
        msg.push_back(0x80);
        while (msg.size() % 64 != 56) msg.push_back(0x00);
        for (int i = 0; i < 8; i++) msg.push_back((uint8_t)((origLen * 8) >> (8 * i)));

        uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t w[80];
            for (int j = 0; j < 16; j++)
                w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|((uint32_t)msg[i+j*4+2]<<8)|((uint32_t)msg[i+j*4+3]);
            for (int j = 16; j < 80; j++)
                w[j] = sha1_rotl(w[j-3]^w[j-8]^w[j-14]^w[j-16], 1);
            uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
            for (int j = 0; j < 80; j++) {
                uint32_t f,k;
                if (j<20){f=(b&c)|((~b)&d);k=0x5A827999;}
                else if(j<40){f=b^c^d;k=0x6ED9EBA1;}
                else if(j<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
                else{f=b^c^d;k=0xCA62C1D6;}
                uint32_t tmp=sha1_rotl(a,5)+f+e+k+w[j];
                e=d;d=c;c=sha1_rotl(b,30);b=a;a=tmp;
            }
            h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
        }
        std::vector<uint8_t> out(20);
        uint32_t vals[5]={h0,h1,h2,h3,h4};
        for (int i=0;i<5;i++) for(int j=0;j<4;j++) out[i*4+j]=(uint8_t)((vals[i]>>(24-8*j))&0xFF);
        return out;
    }

    // ---------- SHA-256 ----------
    static inline uint32_t sha256_rotr(uint32_t x, uint32_t c) { return (x>>c)|(x<<(32-c)); }

    static inline std::vector<uint8_t> sha256_bytes(const std::string& s) {
        std::vector<uint8_t> msg(s.begin(), s.end());
        uint64_t origLen = msg.size();
        msg.push_back(0x80);
        while (msg.size() % 64 != 56) msg.push_back(0x00);
        for (int i = 0; i < 8; i++) msg.push_back((uint8_t)((origLen * 8) >> (8 * i)));

        static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; j++)
                w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|((uint32_t)msg[i+j*4+2]<<8)|((uint32_t)msg[i+j*4+3]);
            for (int j = 16; j < 64; j++) {
                uint32_t s0 = sha256_rotr(w[j-15],7)^sha256_rotr(w[j-15],18)^(w[j-15]>>3);
                uint32_t s1 = sha256_rotr(w[j-2],17)^sha256_rotr(w[j-2],19)^(w[j-2]>>10);
                w[j] = w[j-16]+s0+w[j-7]+s1;
            }
            uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
            for (int j = 0; j < 64; j++) {
                uint32_t S1 = sha256_rotr(e,6)^sha256_rotr(e,11)^sha256_rotr(e,25);
                uint32_t ch = (e&f)^((~e)&g);
                uint32_t t1 = hh+ S1 + ch + K[j] + w[j];
                uint32_t S0 = sha256_rotr(a,2)^sha256_rotr(a,13)^sha256_rotr(a,22);
                uint32_t maj = (a&b)^(a&c)^(b&c);
                uint32_t t2 = S0 + maj;
                hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
            }
            h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
        }
        std::vector<uint8_t> out(32);
        for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) out[i*4+j]=(uint8_t)((h[i]>>(24-8*j))&0xFF);
        return out;
    }

    inline iris::core::Value iris_hashlib_md5(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        return iris::core::Value(to_hex(md5_bytes(args[0].str())));
    }
    inline iris::core::Value iris_hashlib_sha1(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        return iris::core::Value(to_hex(sha1_bytes(args[0].str())));
    }
    inline iris::core::Value iris_hashlib_sha256(iris::core::Value* args, int argCount) {
        if (argCount < 1 || !args[0].isString()) return iris::core::Value("");
        return iris::core::Value(to_hex(sha256_bytes(args[0].str())));
    }
}

#endif
