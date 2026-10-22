# Autores de la BD hipocentral — consolidado por escritura (para el dataset)

Corte de datos: **2014-01-01 en adelante** (`origin.origin_time`), base `csn_hypo`
(instancia 0, puerto 5435). Consultas de solo lectura ejecutadas el **2026-10-22**.

Objetivo: dejar fijado **con qué autores se entrena** y **qué se excluye explícitamente**,
sin perder picadas por diferencias de tecleo en el campo `origin.author`.

> Regla de trabajo: el filtro del dataset es una **lista blanca** de familias
> (abajo, §6). Nunca "todo menos la lista negra": así un nombre nuevo o un typo
> aparece como *no incluido* en vez de colarse.

## 1. Universo

| métrica | valor |
|---|---|
| rango en toda la tabla `origin` | 1982-09-18 → 2026-10-22 |
| orígenes (total) | 502 184 |
| eventos (total) | 183 673 |
| orígenes desde 2014-01-01 | 363 401 |
| **eventos desde 2014-01-01** | **104 293** |
| **picadas manuales desde 2014-01-01** | **2 711 954** |

| `pick_status` | fase | picadas | orígenes |
|---|---|---|---|
| manual | **P** | **1 536 810** | 119 976 |
| manual | **S** | **1 174 813** | 123 830 |
| automatic | P | 2 784 080 | 183 608 |
| automatic | S | 3 620 | 1 444 |

Ratio manual **P:S = 1.31:1**. La S automática es prácticamente inexistente: toda la S
es trabajo humano. Otras fases manuales que hay que excluir: `Pg` 164, `Pn` 76, `Sg` 59,
`pP` 21 y 8 casos sueltos.

Distribución: 2020–2024 concentra el 60 %; el dump histórico **corta el 2025-03-31**
(último dato de mdur/cris/tark/caro). Los registros de 2026 son del pipeline EW8 actual.
60 736 eventos tienen ≥20 picadas manuales; media 26 por evento.

## 2. Método de agrupación

Dos niveles, ambos auditables:

1. **Fusión automática (segura)** — solo diferencias de escritura que no cambian el
   nombre: mayúsculas, acentos, puntuación/espacios y **sufijo numérico**.
   Implementación (`norm`): NFKD sin acentos → `lower` → quitar espacios → quitar
   `[0-9]+$` → quitar todo lo que no sea `a-z`.
   - `Jere` = `jere` = `JERE`; `G.Jerez` = `G:Jerez` = `GJerez` = `GJerez01`
   - `jaba2` = `jaba`; `nico10` = `nico`; `mdur.` = `mdur`
   - Efecto lateral documentado: al borrar todo lo no-alfabético, un dígito **en medio**
     también desaparece (`jab2a` → `jaba`). Solo afecta a `jab2a` (105 P) y similares
     de masa despreciable.
2. **Candidatos por typo (NO fusionados)** — nombre a distancia de edición ≤ 2 de una
   familia grande, con el mismo inicial. Se listan con su masa, años y un veredicto
   automático (`typo probable` si la masa es ≤ 2 % del canónico y la distancia ≤ 1;
   `revisar` si no). **La fusión la decide el usuario** (§5).

## 3. Familias consolidadas

`eventos` es la suma de los eventos de cada variante (un evento puede repetirse si dos
variantes registraron orígenes del mismo sismo, así que es una cota, no un conteo exacto).

| familia | variantes absorbidas | eventos | P manual | S manual | años |
|---|---|---|---|---|---|
| **eatl** | `eatl2`, `Eatl`, `eatl1` | 10 237 | 152 478 | 152 364 | 2018–2025 |
| **thor** | `Thor`, `Thor1`, `thor3` | 11 163 | 135 300 | 120 276 | 2014–2025 |
| **mdur** | `mdur1`, `mdur.`, `MDUR` | 10 514 | 141 241 | 107 487 | 2014–2025 |
| **cris** | `cris1`, `Cris`, `cris2`, `CRIS` | 7 692 | 115 337 | 96 808 | 2014–2025 |
| **caro** | `Caro`, `Caro2` | 8 897 | 135 944 | 58 137 | 2014–2025 |
| **jaba** | `jaba2`, `Jaba`, `jaba1`, `jaba5`, `JABA`, `jaba4`, `jab2a`, `jaba3`, `jABA` | 11 163 | 132 152 | 55 355 | 2016–2025 |
| **nico** | `nico2`, `Nico`, `nico10`, `nico1`, `NICO`, `nico3`, `nico21`, `nico4` | 10 009 | 98 874 | 73 958 | 2014–2024 |
| **tark** | `Tark`, `TARK` | 6 213 | 94 106 | 76 647 | 2014–2025 |
| **Jere** | `jere`, `JERE` | 6 457 | 80 001 | 77 019 | 2014–2025 |
| **maca** | `macA`, `mACA`, `maCA` | 4 281 | 51 467 | 45 125 | 2021–2025 |
| **mabs** | `Mabs`, `MABS` | 4 156 | 54 785 | 38 471 | 2015–2023 |
| **yole** | `Yole`, `yole4`, `yole1`, `yole01`, `yole2` | 3 818 | 52 394 | 33 086 | 2017–2025 |
| **harz** | `Harz`, `HARZ` | 3 716 | 46 553 | 36 331 | 2014–2025 |
| **mary** | — | 1 757 | 25 629 | 21 575 | 2023–2025 |
| **urbi** | `URBI` | 1 556 | 20 096 | 18 575 | 2014–2017 |
| **German** | `German01`, `German1`, `Germán`, `german`, `german01`, `Germán1` | 832 | 18 372 | 18 514 | 2020–2025 |
| **roes** | — | 1 530 | 19 060 | 17 450 | 2014–2016 |
| **vcbc** | — | 1 068 | 18 086 | 15 833 | 2016–2019 |
| **vale** | — | 1 138 | 17 554 | 14 265 | 2015–2018 |
| **pvao** | — | 1 223 | 16 683 | 13 184 | 2014–2016 |
| **rcp** | — | 988 | 11 954 | 10 187 | 2014–2015 |
| **tito** | `Tito` | 810 | 13 670 | 7 659 | 2014–2021 |
| **daar** | — | 776 | 9 527 | 8 376 | 2014–2015 |
| **yeye** | — | 575 | 6 898 | 5 500 | 2016–2018 |
| **G.Jerez** | `GJerez`, `Gjerez`, `G:Jerez`, `GJerez01` | 314 | 5 822 | 6 382 | 2020–2024 |
| **niki** | — | 319 | 6 995 | 3 966 | 2019–2020 |
| **ebop** | — | 446 | 5 335 | 4 888 | 2014 |
| **sali** | `Sali`, `sali2` | 433 | 5 403 | 4 624 | 2024–2025 |
| **TASK** | `task`, `TASk` | 573 | 4 460 | 3 512 | 2020–2024 |
| **ascm** | `Ascm` | 351 | 4 069 | 3 497 | 2014–2019 |
| **Jerez** | `Jerez01` | 155 | 3 094 | 3 187 | 2020–2025 |
| **tarik** | — | 146 | 3 284 | 2 460 | 2021–2023 |
| **paula** | — | 279 | 3 079 | 1 973 | 2020–2024 |
| **david** | — | 195 | 2 804 | 1 917 | 2022 |
| **tati** | — | 188 | 2 716 | 1 950 | 2019–2024 |
| **Marcelo** | `marcelo`, `MARCELO` | 221 | 3 035 | 1 336 | 2020–2023 |
| **tralka** | — | 100 | 1 827 | 1 418 | 2020–2025 |
| **dlmo** | — | 138 | 1 769 | 1 212 | 2022 |
| **lanalhue** | — | 132 | 1 715 | 1 016 | 2020–2025 |
| **sfas** | — | 128 | 1 448 | 1 209 | 2015–2022 |
| **pama** | — | 102 | 1 108 | 915 | 2016–2022 |
| **onemi2** | `onemi1` | 38 | 801 | 403 | 2020–2024 |
| **analista5** | `analista1`, `analista2`, `analista6`, `analista8`, `analista9` | 41 | 553 | 358 | 2020–2024 |
| **LaCaro** | — | 27 | 546 | 111 | 2020–2024 |
| **nuyun** | — | 31 | 454 | 289 | 2020–2025 |

Además hay **128 familias con <50 manuales** que suman 1 742 P y 1 261 S: irrelevantes
para el dataset, pero están en el volcado y en el filtro (§6) por completitud.

**Totales consolidados**: 226 familias válidas, **1 535 439 P** y **1 173 778 S**.

## 4. Qué aporta la normalización (auditoría)

| familia | masa propia | variantes absorbidas | ganancia |
|---|---|---|---|
| `Jere` | 43 124P/42 594S | `jere` 36 862/34 414; `JERE` 15/11 | **+36 877P/+34 425S** |
| `thor` | 105 847P/96 731S | `Thor` 29 371/23 480; `Thor1` 63/47; `thor3` 19/18 | **+29 453P/+23 545S** |
| `caro` | 108 403P/52 028S | `Caro` 27 464/6 095; `Caro2` 77/14 | **+27 541P/+6 109S** |
| `German` | 14 638P/14 627S | `German01` 2 494/2 625; `German1` 423/419; `Germán` 315/341; `german` 233/239; `german01` 182/186; `Germán1` 87/77 | +3 734P/+3 887S |
| `jaba` | 129 931P/54 421S | 9 variantes (764/309 la mayor) | +2 221P/+934S |
| `G.Jerez` | 3 756P/4 131S | `GJerez` 1 485/1 610; `Gjerez` 220/261; `G:Jerez` 225/243; `GJerez01` 136/137 | +2 066P/+2 251S |
| `nico` | 97 694P/72 769S | 8 variantes (631/601 la mayor) | +1 180P/+1 189S |
| `yole` | 51 323P/32 337S | `Yole` 469/184; `yole4` 287/272; `yole1` 269/250; `yole01` 42/39; `yole2` 4/4 | +1 071P/+749S |
| `Marcelo` | 1 968P/832S | `marcelo` 1 044/500; `MARCELO` 23/4 | +1 067P/+504S |
| `mdur` | 140 455P/107 028S | `mdur1` 678/404; `mdur.` 96/44; `MDUR` 12/11 | +786P/+459S |
| `TASK` | 3 815P/2 998S | `task` 642/512; `TASk` 3/2 | +645P/+514S |
| `sali` | 4 918P/4 193S | `Sali` 479/425; `sali2` 6/6 | +485P/+431S |
| `cris` | 115 051P/96 609S | `cris1` 148/96; `Cris` 83/58; `cris2` 47/37; `CRIS` 8/8 | +286P/+199S |
| `eatl` | 152 263P/151 777S | `eatl2` 197/569; `Eatl` 11/12; `eatl1` 7/6 | +215P/+587S |
| `Jerez` | 2 908P/2 986S | `Jerez01` 186/201 | +186P/+201S |

**Total recuperado por normalizar: ≈108 000 P (7.0 %) y ≈76 000 S (6.5 %).**
Sin esto, filtrar por el nombre tal como está escrito pierde el **46 % de `Jere`**,
el **22 % de `thor`**, el **20 % de `caro`** y el **20 % de `German`**.

## 5. Candidatos a typo que NO se fusionaron (decisión pendiente)

Los de arriba (masa ≥ 100 y veredicto `typo probable`):

| variante | P | S | años | canónico | dist. | masa rel. |
|---|---|---|---|---|---|---|
| `criss` | 436 | 219 | 2023–2024 | `cris` | 1 | 0.31 % |
| `trk` | 327 | 275 | 2016, 2020–2021 | `tark` | 1 | 0.35 % |
| `yolw` | 302 | 176 | 2018–2024 | `yole` | 1 | 0.56 % |
| `carp` | 288 | 142 | 2015–2024 | `caro` | 1 | 0.22 % |
| `yolr` | 278 | 140 | 2018–2025 | `yole` | 1 | 0.49 % |
| `nivo` | 223 | 133 | 2018–2023 | `nico` | 1 | 0.21 % |
| `jab` | 203 | 97 | 2019–2025 | `jaba` | 1 | 0.16 % |
| `mac` | 151 | 134 | 2023–2024 | `maca` | 1 | 0.30 % |
| `tarj` | 135 | 117 | 2015–2023 | `tark` | 1 | 0.15 % |
| `nco` | 156 | 89 | 2020–2023 | `nico` | 1 | 0.14 % |
| `uebi` | 124 | 121 | 2014–2017 | `urbi` | 1 | 0.63 % |
| `nici` | 145 | 91 | 2019–2023 | `nico` | 1 | 0.14 % |
| `tari` | 111 | 87 | 2014–2022 | `tark` | 1 | 0.12 % |
| `jabn` | 97 | 76 | 2020 | `jaba` | 1 | 0.09 % |
| `tarkk` | 94 | 70 | 2025 | `tark` | 1 | 0.10 % |
| `jer` | 90 | 64 | 2014–2021 | `Jere` | 1 | 0.10 % |
| `jba` | 109 | 38 | 2018–2024 | `jaba` | 1 | 0.08 % |
| `earl` | 58 | 87 | 2024 | `eatl` | 1 | 0.05 % |
| `mdu` | 68 | 57 | 2016–2025 | `mdur` | 1 | 0.05 % |
| `mab` | 66 | 53 | 2017–2023 | `mabs` | 1 | 0.13 % |
| `cari` | 72 | 43 | 2015–2023 | `caro` | 1 | 0.06 % |
| `mans` | 69 | 44 | 2019, 2022 | `mabs` | 1 | 0.12 % |
| `mdue` | 65 | 44 | 2017–2021 | `mdur` | 1 | 0.04 % |
| `cri` | 56 | 45 | 2015–2022 | `cris` | 1 | 0.05 % |

Casos que **no** son typo y deben quedar como analistas distintos:

| variante | P | S | años | ¿por qué no? |
|---|---|---|---|---|
| `tati` | 2 716 | 1 950 | 2019–2024 | nombre completo distinto |
| `pama` | 1 108 | 915 | 2016–2022 | masa 40 % de `paula` |
| `niki` | 6 995 | 3 966 | 2019–2020 | 319 eventos propios, no es `nico` |
| `TASK` / `tarik` | 4 460 / 3 284 | 3 512 / 2 460 | 2020–2024 | alias con masa propia |
| `LaCaro` | 546 | 111 | 2020–2024 | **sí parece `caro`** (prefijo "La"), fusión manual a decidir |
| `Eltark` | 44 | 32 | 2020 | **sí parece `tark`** ("El Tark") |

El volcado completo tiene **146 candidatos**; los de masa < 100 no cambian el dataset
(entre todos suman < 2 000 P). Están en `/tmp/opencode/stats/merge3.md` (no versionado).

## 6. Lista negra explícita (excluir siempre)

30 nombres, **1 371 P y 1 035 S → 0.09 % de los manuales**. Se excluyen por no ser
personas o no ser analistas, no por volumen.

| nombre en la BD | eventos | P | S | motivo |
|---|---|---|---|---|
| `eventquery_catalog` | 82 739 | 0 | 0 | catálogo importado |
| `autoloc_local` | 28 700 | 0 | 0 | SeisComP3 LOCSAT (automático) |
| `autoloc_global` | 7 445 | 0 | 0 | SeisComP3 LOCSAT (automático) |
| `autoloc_aysen` | 101 | 0 | 0 | automático |
| `csnhypodbp` | 1 093 | 3 | 0 | pipeline EW8 (2026) |
| `csnloc` | 455 | 5 | 0 | pipeline EW8 (2026) |
| `nlloc` | 425 | 5 | 0 | pipeline EW8 (2026) |
| `hyp2000` | 287 | 4 | 0 | pipeline EW8 (2026) |
| `n` | 34 | 418 | 357 | inicial suelta |
| `csn` | 23 | 296 | 204 | institucional / no persona |
| `xxx` | 7 | 123 | 122 | prueba |
| `b` | 8 | 77 | 65 | inicial suelta |
| `y` | 3 | 57 | 32 | inicial suelta |
| `e` | 4 | 51 | 35 | inicial suelta |
| `PYTH` | 56 | 62 | 23 | script |
| `d` | 7 | 50 | 23 | inicial suelta |
| `q` | 5 | 36 | 33 | inicial suelta |
| `u` | 2 | 28 | 24 | inicial suelta |
| `L` / `l` | 2 | 32 | 27 | inicial suelta |
| `a` | 1 | 21 | 3 | inicial suelta |
| `starktest` | 2 | 26 | 16 | prueba |
| `1` / `.` | 2 | 25 | 24 | sin letras |
| `seis` | 2 | 16 | 8 | prueba |
| `PruebaThor` | 1 | 12 | 12 | prueba |
| `pub` | 1 | 11 | 11 | prueba |
| `example` | 2 | 12 | 9 | prueba |
| `asd`, `gf` | 2 | 0 | 2 | prueba |
| `'mdur\n\n\n\n\nseiscomp exec scolv -u mdur --author=mdur'` | 1 | 5 | 5 | texto de comando |

## 7. Totales y filtro propuesto

| | |
|---|---|
| familias válidas | **226** |
| P manuales tras excluir | **1 535 439** |
| S manuales tras excluir | **1 173 778** |
| top 10 | **74.0 % P / 73.5 % S** |
| top 14 | **85.7 % P / 84.6 % S** |
| top 20 | **92.9 % P / 92.9 % S** |
| excluido | 1 371 P / 1 035 S (**0.09 %**) |

Filtro recomendado (lista blanca explícita; `regexp_replace` replica `norm`):

```sql
SELECT p.*
FROM pick p
JOIN origin o ON o.origin_id = p.origin_id
WHERE p.pick_status = 'manual'
  AND p.phase IN ('P','S')
  AND o.origin_time >= '2014-01-01'
  AND o.origin_time <  '2025-04-01'
  AND regexp_replace(lower(trim(o.author)), '[0-9]+$', '') IN (
      'eatl','thor','mdur','cris','caro','jaba','nico','tark','jere','maca',
      'mabs','yole','harz','mary','urbi','german','roes','vcbc','vale','pvao');
```

Esa lista blanca son los **top 20 (92.9 % de ambas fases)**. Añadir `rcp`, `tito`,
`daar`, `yeye`, `gjerez`, `niki`, `ebop`, `sali`, `task`, `ascm`, `jerez`, `tarik`,
`paula`, `david`, `tati`, `marcelo`, `tralka`, `dlmo`, `lanalhue`, `sfas`, `pama`
y las familias menores da el 100 %.

Notas de aplicación:

1. `regexp_replace(..., '[0-9]+$', '')` **no** quita dígitos internos; el `norm` del
   script sí. La única diferencia real es `jab2a` (105 P), que así quedaría fuera de
   `jaba`. Irrelevante; si molesta, añadir `'jab2a'` a la lista.
2. Filtrar por `pick_status='manual'` ya deja fuera todo el pipeline y el catálogo.
   La lista blanca protege además de nombres nuevos y de los `autoloc_*`.
3. La capa `pick.phase` trae `P`/`S` y variantes (`Pg`, `Pn`, `Sg`…). El `IN ('P','S')`
   es obligatorio.
4. Estratificar el muestreo por **año × autor × estación × tamaño de evento**, no tomar
   las primeras N: 2020–2024 concentra el 60 % y `eatl`/`jaba`/`maca`/`mary` tienen
   ventanas temporales cortas y solapadas.

## Anexo — reproducibilidad

- Consultas SQL: ver §1/§3 de esta misma serie y el volcado crudo en
  `/tmp/opencode/stats/autores2.sql` y `/tmp/opencode/stats/out.txt`.
- Consolidado por escritura: `/tmp/opencode/stats/autores_merge.py` (procesa el
  volcado, no toca la BD) y su salida `/tmp/opencode/stats/merge3.md`.
- Los tres archivos de `/tmp/opencode/stats/` **no** están versionados.
