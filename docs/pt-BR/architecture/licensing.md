# Notas de licenciamento

A [LICENSE](../../../LICENSE) na raiz é **AGPL-3.0** para o projeto publicado
(documentação, userspace, packaging).

## Exceção do módulo kernel (obrigatória no Linux)

Módulos out-of-tree que usam exports **somente GPL** do kernel precisam de
`MODULE_LICENSE` aceite pelo kernel (em geral `MODULE_LICENSE("GPL")`) com
código em **GPL-2.0-or-later** na árvore `kmod/`.

| Árvore | Licença |
|--------|---------|
| `kmod/` | **GPL-2.0-or-later** |
| Userspace, docs, packaging | **AGPL-3.0-or-later** |

Assim o `.ko` carrega e liga ao kernel, mantendo copyleft forte no stack de
integração. Um `.ko` “só AGPL” não é prático no Linux mainline.
