#ifndef _OMPTARGET_RTTYPE_H_
#define _OMPTARGET_RTTYPE_H_
// constraint in 1 byte
// int64_t for unify
//#define RTT_BYTE_MASK     0x7   // 0000 0111

// width_max: 16 byte
// 1, 2, 4, 8, 16          /* mask byte / shift*/
//#define RTT_GET_BYTE(T)    1 << (T & RTT_BYTE_MASK)

// type hepler
#define RTT_IS_BUILTIN(T)   (T & RTT_BUILTIN)
#define RTT_IS_STRUCT(T)    (T & RTT_STRUCT)
#define RTT_IS_PTR(T)       (T & RTT_PTR)
#define RTT_IS_TID(T)       (T & RTT_TID)

#define DIVID_PTR_SIZE(INT) (INT >> 3) // FIXME not portable

enum RttReturn {
  RTT_FAILED,
  RTT_SUCCESS,
  RTT_END
};

// checkbit macro for fast typing
enum RttTypes : uint64_t {
  RTT_BUILTIN       = 0x01,
  RTT_PTR           = 0x02,
  RTT_STRUCT        = 0x2UL << 60,
  RTT_TID           = 0x4UL << 60,
};

bool RttValidMaptype(int Type);

struct RttJob {
  enum kind{
    UpdatePtrJob,
    DataTransferJob,
    RootRegionJob,
    SkipJob,
    EndJob
  } Kind;
  void *base;
  int64_t idx;
  int64_t size;
  int64_t size_offset;
  enum tgt_map_type map_type_mask;
  RttJob(enum kind k) : Kind(k) {idx = 0;};
  RttJob(enum kind k, int64_t size) : Kind(k), size(size) {
    idx = 0;
  };
};

typedef std::list<RttJob> RttJobsTy;
typedef std::list<RttJob>::iterator RttJobsItrTy;

struct RttTy {
  // rtt args
  RttTypes **rtts;
  int64_t *rtt_sizes;

  RttTypes *rtt;
  int64_t origin_type;

  // stack address to return
  void **ptr_begin;
  void **ptr_base;
  int64_t *data_size;
  int64_t *data_type;

  RttJobsTy *Jobs;
  // Iterator
  RttJobsItrTy CurJob;
  bool BackReturning;
  bool isFirst;

  RttTy(void **rtts, int64_t *rtt_sizes) :
    rtts((RttTypes**)rtts), rtt_sizes(rtt_sizes), isFirst(true) {}
  enum RttReturn computeRegion();
  int init(void **ptr_begin, void **ptr_base,
    int64_t *data_size, int64_t *data_type);

  private:
  int computeRegion1();
  static int validMaptype(int Type);
  RttJobsTy *genJobs(RttTypes *);
  void dumpType();
  void dumpJobs();
  enum RttReturn fillData(int64_t *&, void*);
};

#if 0
// From clang/ast/builtintypes.def
// void
BUILTIN_TYPE(Void, VoidTy)
//===- Unsigned Types -----------------------------------------------------===//
// 'bool' in C++, '_Bool' in C99
UNSIGNED_TYPE(Bool, BoolTy)
// 'char' for targets where it's unsigned
SHARED_SINGLETON_TYPE(UNSIGNED_TYPE(Char_U, CharTy))
// 'unsigned char', explicitly qualified
UNSIGNED_TYPE(UChar, UnsignedCharTy)
// 'wchar_t' for targets where it's unsigned
SHARED_SINGLETON_TYPE(UNSIGNED_TYPE(WChar_U, WCharTy))
// 'char8_t' in C++20 (proposed)
UNSIGNED_TYPE(Char8, Char8Ty)
// 'char16_t' in C++
UNSIGNED_TYPE(Char16, Char16Ty)
// 'char32_t' in C++
UNSIGNED_TYPE(Char32, Char32Ty)
// 'unsigned short'
UNSIGNED_TYPE(UShort, UnsignedShortTy)
// 'unsigned int'
UNSIGNED_TYPE(UInt, UnsignedIntTy)
// 'unsigned long'
UNSIGNED_TYPE(ULong, UnsignedLongTy)
// 'unsigned long long'
UNSIGNED_TYPE(ULongLong, UnsignedLongLongTy)
// '__uint128_t'
UNSIGNED_TYPE(UInt128, UnsignedInt128Ty)
//===- Signed Types -------------------------------------------------------===//
// 'char' for targets where it's signed
SHARED_SINGLETON_TYPE(SIGNED_TYPE(Char_S, CharTy))
// 'signed char', explicitly qualified
SIGNED_TYPE(SChar, SignedCharTy)
// 'wchar_t' for targets where it's signed
SHARED_SINGLETON_TYPE(SIGNED_TYPE(WChar_S, WCharTy))
// 'short' or 'signed short'
SIGNED_TYPE(Short, ShortTy)
// 'int' or 'signed int'
SIGNED_TYPE(Int, IntTy)
// 'long' or 'signed long'
SIGNED_TYPE(Long, LongTy)
// 'long long' or 'signed long long'
SIGNED_TYPE(LongLong, LongLongTy)
// '__int128_t'
SIGNED_TYPE(Int128, Int128Ty)
//===- Fixed point types --------------------------------------------------===//
// 'short _Accum'
SIGNED_TYPE(ShortAccum, ShortAccumTy)
// '_Accum'
SIGNED_TYPE(Accum, AccumTy)
// 'long _Accum'
SIGNED_TYPE(LongAccum, LongAccumTy)
// 'unsigned short _Accum'
UNSIGNED_TYPE(UShortAccum, UnsignedShortAccumTy)
// 'unsigned _Accum'
UNSIGNED_TYPE(UAccum, UnsignedAccumTy)
// 'unsigned long _Accum'
UNSIGNED_TYPE(ULongAccum, UnsignedLongAccumTy)
// 'short _Fract'
SIGNED_TYPE(ShortFract, ShortFractTy)
// '_Fract'
SIGNED_TYPE(Fract, FractTy)
// 'long _Fract'
SIGNED_TYPE(LongFract, LongFractTy)
// 'unsigned short _Fract'
UNSIGNED_TYPE(UShortFract, UnsignedShortFractTy)
// 'unsigned _Fract'
UNSIGNED_TYPE(UFract, UnsignedFractTy)
// 'unsigned long _Fract'
UNSIGNED_TYPE(ULongFract, UnsignedLongFractTy)
// '_Sat short _Accum'
SIGNED_TYPE(SatShortAccum, SatShortAccumTy)
// '_Sat _Accum'
SIGNED_TYPE(SatAccum, SatAccumTy)
// '_Sat long _Accum'
SIGNED_TYPE(SatLongAccum, SatLongAccumTy)
// '_Sat unsigned short _Accum'
UNSIGNED_TYPE(SatUShortAccum, SatUnsignedShortAccumTy)
// '_Sat unsigned _Accum'
UNSIGNED_TYPE(SatUAccum, SatUnsignedAccumTy)
// '_Sat unsigned long _Accum'
UNSIGNED_TYPE(SatULongAccum, SatUnsignedLongAccumTy)
// '_Sat short _Fract'
SIGNED_TYPE(SatShortFract, SatShortFractTy)
// '_Sat _Fract'
SIGNED_TYPE(SatFract, SatFractTy)
// '_Sat long _Fract'
SIGNED_TYPE(SatLongFract, SatLongFractTy)
// '_Sat unsigned short _Fract'
UNSIGNED_TYPE(SatUShortFract, SatUnsignedShortFractTy)
// '_Sat unsigned _Fract'
UNSIGNED_TYPE(SatUFract, SatUnsignedFractTy)
// '_Sat unsigned long _Fract'
UNSIGNED_TYPE(SatULongFract, SatUnsignedLongFractTy)
//===- Floating point types -----------------------------------------------===//
// 'half' in OpenCL, '__fp16' in ARM NEON.
FLOATING_TYPE(Half, HalfTy)
// 'float'
FLOATING_TYPE(Float, FloatTy)
// 'double'
FLOATING_TYPE(Double, DoubleTy)
// 'long double'
FLOATING_TYPE(LongDouble, LongDoubleTy)
// '_Float16'
FLOATING_TYPE(Float16, HalfTy)
// '__float128'
FLOATING_TYPE(Float128, Float128Ty)
#endif
#endif
