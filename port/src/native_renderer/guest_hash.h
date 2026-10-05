#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace superman_returns::native {
// Hasheia memória do jogo onde ela está, sem a cópia de ReadProcessMemory de CheckedGuestReads.
// `hash(data, size, seed)` roda dentro de um frame SEH: uma faixa não confirmada ou ilegível devolve
// false e deixa `out` intacto, como uma leitura checada que falha. O valor é o mesmo que o hash de
// uma cópia daria.
template <class Hash>
bool HashGuestRange(const void* source, uint32_t length, uint64_t seed, Hash hash, uint64_t& out) {
  if (!source || !length || length > 0x20000000u) return false;
  uint64_t value = 0;
  __try {
    value = hash(source, size_t(length), seed);
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
  out = value;
  return true;
}
}  // namespace superman_returns::native
