# Vendored sources

| Directory | Upstream | Commit |
|---|---|---|
| nblood-amiga | https://github.com/BSzili/NBlood-Amiga | 4ee135bce65a0a480032475b958d8ec7706128aa |
| nblood-amiga/jfbuild | https://github.com/BSzili/jfbuild (branch amiga, as pinned by NBlood-Amiga) | 1e5b708c5096071e1ffc379aff40d183bf559cd5 |
| nblood-amiga/jfaudiolib | https://github.com/BSzili/jfaudiolib (branch amiga) | e424f0e585f7562acdf1e18900b656bd8e86a35d |
| nblood-amiga/jfmact | https://github.com/BSzili/jfmact | 1f0746a3b9704906669d8aaed2bbb982053a393e |

Local changes are tracked by this repository's history. `NB_SMALL` replaces most
`__AMIGA__` conditionals (small-machine code paths); `__AMIGA__` is kept only
where the code is AmigaOS-specific.
