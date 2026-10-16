# Mapeo de datos ↔ PostgreSQL — PENDIENTES

Lo que **no está decidido** o **no se resuelve** en la iteración de auditoría del mapeo
(2026-10-12). El mapeo en sí está en `docs/DATA-MAPPING.md`.

## Resueltos en esta iteración

| id | qué | cómo |
|---|---|---|
| D2 | `author` / `software` | `author = 'csnhypodbp'` (constante); `software = $EW_VERSION` (fallback `ew_8.0`); `location_algorithm` = `csnloc`/`HYPOINVERSE`/`NLLoc` |
| D3 | `calculation_method` | se copia `MAG_INFO.algorithm` del mensaje |
| D5 | `event_id` | `E<qid>-<yyyymmddhhmmss>` (UTC de la hora origen de la 1.ª solución del grupo) + guarda `EventMaxDtSec` en el atajo de agrupación por qid |
| D10 | orden de las tablas | desempates explícitos por tabla y default de *Origins* alineado con la consulta |

## Pendientes por resolver

### P1 = D1 · La solución confirmada no tiene autoría propia
`csnhypodbp_confirm_manual` (`csnhypodbp_ws.c:1724`) **no publica** ningún `TYPE_HYP2000ARC`:
sólo magnitudes a `HYPO_RING_REF` (`algorithm="CSNMan"`, `:1679`) y picks a `PICK_RING`
(12.º token `M`, `:1715`). La BD recibe `origin` de `csnloc`(162)/`hyp2000`(163)/`nlloc`(164) con
`status=REVIEWED`; **no existe** ninguna fila con `author='csnhypodbp'` hasta resolverlo.

- Impacto: no se puede verificar "confirmé y quedó"; la solución que ve el usuario puede diferir de
  la que `csnloc` recalcula con esos picks (mismo conjunto de picks, otro localizador/modelo).
- Opciones: (a) publicar el ARC confirmado con `mod 158` (a `HYPO_RING` → se refina y crea fila
  163; a `HYPO_RING_REF` → fila 158 directa) + `mod_author(158)`; (b) escribir directo a PostgreSQL
  desde la GUI (rompe el principio "solo `hypodb` escribe"); (c) dejarlo como está y marcar la
  autoría sólo en `pick.pick_status`/`status`.

### P2 = D4 · La versión de la solución no sobrevive al viaje por la BD
`origin_id = <event>:<mod>` (`ewgui/db_ids.c:38`) y el UPSERT pisa; `Ver` en historial es la
constante `"0"` (`csnhypodbp_ws.c:716,752`), así que la columna `Ver` de *Origins* miente.

- Opciones: (a) columna `version` en `origin` (toca `db/schema.sql`, que es copia literal de
  `test_db`); (b) incluir la versión en `origin_id` y parsearla al leer; (c) no persistir y mostrar
  `Ver = "-"` en historial.

### P3 = D6 · `origin.creation_time` no se refresca en el UPSERT
`ON CONFLICT (origin_id) DO UPDATE SET ...` (`ewgui_db.c:286-299`) no incluye `creation_time`, así
que `Created` y el orden por defecto de *Origins* quedan clavados en la primera inserción.

- Opciones: refrescarlo en el UPDATE; añadir `updated_time`; dejarlo y ordenar por otra columna.
- Relacionado: la columna `Created` tiene dos fuentes (fila viva = llegada a la GUI, fila histórica
  = inserción en la BD).

### P4 = D7 · Columnas que ningún código escribe (siempre NULL)
`origin.latitude_uncertainty`, `origin.longitude_uncertainty`, `origin.depth_uncertainty`,
`origin.time_uncertainty`, `origin.error_ellipse`, `pick.polarity`, `pick.probability` y toda
`tensor.*`.

- `pick.polarity` es hoy **imposible**: el ARC que circula no lleva primer movimiento
  (`csnloc/hypo_out.c:208-219`, parseo en `csnhypodbp_group.c:155-183`). Para llenarla habría que
  añadir el campo al ARC.
- Decisión: declararlas "vacías por diseño" (con motivo, ya recogido en el invariante de cobertura)
  o llenar las que sí tengan fuente (las incertidumbres de nlloc/hyp2000 viven en sus sumarios, no
  en el ARC).

### P5 = D8 · La magnitud manual pisa la automática
`magnitude_id = <origin>/mag/<TIPO>` + `ON CONFLICT DO UPDATE` (`ewgui_db.c:321-330`): un recálculo
del mismo tipo sobrescribe, y `magnitude` no tiene `creation_time` ni versión. Queda sólo
`calculation_method` para distinguirlas.

- Opciones: clave por `(origin, tipo, método)`; columna de versión/creación; conservar sólo la
  última (como hoy) documentándolo.

### P6 = D9 · `amplitude_type`: convención
Hoy `ML→AML`, `Mb→AMb`, `Ms→AMS`, otro → `AMP` (`hypodb_ingest.c:176-182`), pero el COMMENT del
esquema (`db/schema.sql:101`) lista códigos SEISAN con prefijo (`IAML`, `AML`, `IAmb`, `AMb`,
`AMS`, `IVMs`, `IVmB`, `AMP`). Falta decidir si Mwp/Mw llevan código propio y si se usa la forma
con `I`.

### P7 = D11 · Fila viva y fila histórica del mismo sismo
`cargar_grupos_db` (`csnhypodbp_ws.c:665`) crea `EventGroup` nuevos y los añade a `g_groups` sin
comparar contra los grupos vivos (misma firma de picadas), así que el mismo sismo podría aparecer
dos veces en *Events* si una recarga de historial coincide con un evento vivo.

- A verificar: si `cargar_sismos_db` corre sólo al arranque (antes de que llegue nada del anillo).

### P8 = D12 · El atajo `qid == qid` fusiona sismos distintos — MITIGADO en hypodb
`csnhypodbp_group_matches` (`csnhypodbp_group.c:308-320`) acepta el match por qid **sin** comprobar
la firma de picadas. Mitigado en `hypodb_assign_event` con la guarda `|Δotime| ≤ EventMaxDtSec`
(default 300 s, clave nueva de `hypodb.d`), que bloquea el atajo sólo entre sismos lejanos y deja
intacto el caso legítimo (una repicada manual dentro de la ventana sigue agrupando).

- Pendiente de decidir: si se quiere traza de auditoría de las fusiones (log por fusión) y si la
  misma guarda debe aplicarse en la agrupación en memoria de la GUI de `csnhypodbp` (hoy no la
  tiene: no comparte el estado de `hypodb`).

## Supuestos vigentes (vetables)

- S1: `pick.polarity` se declara vacía por diseño (el ARC no lleva primer movimiento).
- S2: `creation_time` = hora de inserción en la BD es aceptable como "tiempo de creación".
- S3: `event.felt`/`published` = `false` y `event_type` = `'EARTHQUAKE'` fijos: correcto.
- S4: `tensor` vacía por diseño (sin productor).
- S5: `magnitude.mag_uncertainty` = `mag.error` de csnmags cuando ≥ 0.
- S6: `author` pasa a ser constante (`csnhypodbp`), así que en esa columna se pierde la identidad
  del localizador; sigue disponible en `location_algorithm`.

## Prerrequisito de entorno

PostgreSQL arriba (`db/docker-compose.yml`, puerto 5435) y
`EWGUI_DB_TEST_ENV=run_working_v8/params/db.env`. Sin eso, `test_ingest_db`,
`test_db_integration` y `test_db_search` hacen SKIP.
