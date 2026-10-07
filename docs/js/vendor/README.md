# XXH3 para bibliotecas D3D12

`xxhash3.umd.min.mjs` é o bundle sem alterações de `hash-wasm` 4.12.0,
distribuído sob MIT (texto em `LICENSE-hash-wasm.txt`). A extensão `.mjs`
permite importar o bundle no navegador e nos testes Node como módulo; ele
expõe `globalThis.hashwasm.xxhash3`. O WebAssembly está contido no próprio bundle.

Origem: <https://github.com/Daninet/hash-wasm/tree/v4.12.0>

Obtenção: <https://cdn.jsdelivr.net/npm/hash-wasm@4.12.0/dist/xxhash3.umd.min.js>

SHA-256: `8749a42c2fd60262e8dc8f796faeb4f3c05a8d5c195dacd7fbb5cdd9d6a9199e`.

O gerador usa XXH3-64 sem seed para reproduzir as chaves do corpus nativo em
`make_preshaders.py`. A biblioteca compara também os bytes originais dos shaders.
