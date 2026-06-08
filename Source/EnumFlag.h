#pragma once

#define DEFINE_ENUM_FLAG(ENUM_TYPE) \
constexpr inline ENUM_TYPE operator~ (ENUM_TYPE a) \
{ \
    return static_cast<ENUM_TYPE>(~static_cast<std::underlying_type<ENUM_TYPE>::type>(a)); \
} \
constexpr inline ENUM_TYPE operator| (ENUM_TYPE a, ENUM_TYPE b) \
{ \
    return static_cast<ENUM_TYPE>(static_cast<std::underlying_type<ENUM_TYPE>::type>(a) | static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \
constexpr inline ENUM_TYPE operator& (ENUM_TYPE a, ENUM_TYPE b) \
{ \
    return static_cast<ENUM_TYPE>(static_cast<std::underlying_type<ENUM_TYPE>::type>(a) & static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \
constexpr inline ENUM_TYPE operator^ (ENUM_TYPE a, ENUM_TYPE b) \
{ \
    return static_cast<ENUM_TYPE>(static_cast<std::underlying_type<ENUM_TYPE>::type>(a) ^ static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \
inline ENUM_TYPE& operator|= (ENUM_TYPE& a, ENUM_TYPE b) \
{ \
    return reinterpret_cast<ENUM_TYPE&>(reinterpret_cast<std::underlying_type<ENUM_TYPE>::type&>(a) |= static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \
inline ENUM_TYPE& operator&= (ENUM_TYPE& a, ENUM_TYPE b) \
{ \
    return reinterpret_cast<ENUM_TYPE&>(reinterpret_cast<std::underlying_type<ENUM_TYPE>::type&>(a) &= static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \
inline ENUM_TYPE& operator^= (ENUM_TYPE& a, ENUM_TYPE b) \
{ \
    return reinterpret_cast<ENUM_TYPE&>(reinterpret_cast<std::underlying_type<ENUM_TYPE>::type&>(a) ^= static_cast<std::underlying_type<ENUM_TYPE>::type>(b)); \
} \

