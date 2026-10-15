# Permisos de los agentes de OpenCode (V2)

Contrato de los agentes de `.opencode/agents/` y por qué están escritos así.
La fuente de verdad son los propios archivos; esto es lo que hay que saber para
tocarlos sin romperlos.

## 1. Cómo evalúa V2 un permiso de shell (medido)

Estas reglas no son intuiciones: se midieron con una sesión real (2026-10-14) en
el agente `plan`, y el test `ew_gui_tools/common/tests/test_agent_permissions.sh`
las modela para que sigan cumpliéndose.

1. **Gana la ÚLTIMA regla que coincide** (no la más específica). Las reglas
   globales de `opencode.jsonc` se **anteponen** y las del agente van al final
   ("agent rules are appended last"). Consecuencia práctica: un `shell *` allow
   en un agente **borra** los `deny` globales salvo que el agente vuelva a
   declararlos después. Lo hacen `build` y `runner`; el test lo exige.
2. **El comando se parte en sub-comandos por `&&`, `||`, `;` y `|`, y cada uno
   cuenta como un recurso.** *Cualquier* `deny` tumba la llamada completa. Por
   eso `ls x | head -3` falla si `head` no tiene regla, y `ls x && echo ok`
   falla por el `echo`.
3. **No hay reglas de "envoltorio"**: `source x && cmd`, `VAR=v cmd`,
   `/usr/bin/timeout N cmd`, `xargs` son comandos propios que necesitan su
   propia regla.
4. Un patrón que termina en ` *` también casa **sin argumentos**
   (`git status *` cubre `git status` y `git status --short`). Un patrón **sin**
   ` *` casa **exacto** (esto es lo que se usa, a propósito, para los verbos de
   git que también pueden escribir).
5. `cd <ruta relativa>` se reconoce como cambio de directorio (no necesita
   regla); `cd /ruta/absoluta` **sí** genera un recurso y queda denegado.
6. La redirección (`2>/dev/null`, y también `> archivo`) viaja **dentro** del
   segmento: si una regla aprueba el comando, la aprueba con su redirección.
   Por eso `ls x > archivo` escribía con solo `ls *` permitido, y por eso hay
   reglas explícitas `* > *`, `* >> *`, `* 2> *`, `* &> *`.
7. Un comando que el scanner **no puede analizar** (sustitución de procesos
   `<(…)`, sustitución de comandos `$(…)`, backticks) le llega como *opaco* y lo
   **deniega**: medido con `diff <(echo a) <(echo b)`. También deniega el
   directorio cuando no puede inferirlo (p. ej. `ls` de una ruta fuera del
   workspace, que sale con el mismo mensaje opaco).
8. El error es siempre el mismo y **opaco**:
   `{"error":{"type":"permission.rejected","message":"Permission denied: shell"}}`.
   No dice qué segmento ni qué regla falló. De ahí que los prompts de los
   agentes tengan la sección "higiene de comandos": un `DENEGADO` es de **ese**
   comando, no del shell, y no hay que reintentar variantes.
8. Si ninguna regla coincide, el efecto es `ask`. En la práctica, un cliente sin
   interactividad lo resuelve como denegación; por eso los agentes de solo
   lectura llevan un `shell *` **deny** como primera regla (catch-all), y así no
   queda nada "por preguntar".
9. `effect: ask` **sí** es la herramienta para "solo si el usuario lo pide": es
   el único `ask` del conjunto (`plan` lo usa para `diag.sh run *`) y muestra el
   comando exacto con las opciones *once / always / reject*.

### Línea base medida antes del cambio (agente `plan`, 28 llamadas, 10 denegadas)

| comando | resultado | motivo |
|---|---|---|
| `ls`, `ls -1 x`, `ls -1 x 2>/dev/null` | OK | `ls *` cubre sin argumentos; la redirección `2>` no es recurso aparte |
| `git status`, `git log --oneline -3`, `git diff --stat` | OK | en la allowlist |
| `git diff` (pelado) | DENEGADO | solo existía `git diff --stat *` |
| `git branch --show-current` | DENEGADO | no había regla |
| `/usr/bin/timeout 5 ls` | DENEGADO | envoltorio = recurso propio |
| `LANG=C ls` | DENEGADO | prefijo de entorno = recurso propio |
| `source ./ew8_unix.sh` | DENEGADO | sin regla |
| `ls x \| head -3` | DENEGADO | `head` no tenía regla (compuesto = 2 recursos) |
| `cd . && ls` · `cd run_working_v8/params && ls x` | OK | `cd` relativo exento |
| `cd /home/seba/... && ls` | DENEGADO | `cd` absoluto = recurso |
| `ls x > /tmp/…` | **OK — escribía el archivo** | la redirección viaja en el segmento |
| `echo hola`, `rm -f x`, `ls \| tee f` | DENEGADO | sin regla |

## 2. Los agentes

| agente | modo | shell | puede |
|---|---|---|---|
| `plan` | primary | jaula cerrada (`shell *` deny + allowlist) | `ls`, git de lectura, `diag.sh` (nivel A allow, `run` **ask**) |
| `build` | primary | `shell *` allow + deny re-declarados | editar, compilar vía `@runner`; **no** `git add/commit/push` ni `-c`/`--no-pager` |
| `explore` | subagent | jaula cerrada | buscar/leer, git de lectura; sin `cat`/`find` |
| `scout` | subagent | sin shell | solo web |
| `runner` | subagent | `shell *` allow + deny re-declarados | ejecutar lo que le piden; **no** git de escritura |
| `review` | subagent | jaula cerrada | git de lectura y archivos; **nada** de escritura |

### Verbos de git "con doble forma": allow exacto

`git branch`, `git tag`, `git stash`, `git worktree` y `git remote` tienen forma
de lectura **y** de escritura. Ahí el allow va **sin ` *`** (exacto), para que un
encadenado de flags no se cuele:

- `git branch -a` → permitido · `git branch -a -d x` → **denegado** (añade
  caracteres, ya no casa el patrón exacto, cae en el catch-all).
- igual con `git tag -l`, `git stash list`, `git worktree list`, `git remote -v`.

Complementos (deny por subcadena) para formas de escritura dentro de un verbo que
si tiene allow amplio: `*--output*` (escribe un archivo),
`*--ext-diff*` (ejecuta un driver externo), `*--open-files-in-pager*` y `* -O*`
(`git grep`), `*sort*-o *`.

## 3. `review` (el revisor)

Subagente de **solo lectura**. No escribe informes a archivo: el informe va en el
chat con `VEREDICTO: GO|NO-GO` + checklist. Lo que lo hace hermético:

- allowlist: solo git de lectura y `read`/`grep`/`glob`;
- `edit: deny` (cubre `edit`+`write`+`patch`);
- `subagent: * deny`: no puede pedirle a otro agente que haga el cambio;
- `shell *` deny como primera regla, y los `deny` explícitos
  (`git -c`, `--no-pager`, redirecciones) **después** de los allows: la doc
  garantiza que un "Allow always" nunca levanta un `deny` configurado;
- los verbos de escritura (`add`, `commit`, `push`, `checkout`, `reset`, …) **no
  tienen regla**: no se pueden aprobar ni por accidente.

## 4. Policies: credenciales

`opencode.jsonc` declara `policies` con `tool.use` (`read:*service.json`,
`read:*db.env`). Las policies son un hard-deny que se evalúa **después** de las
reglas y de las aprobaciones: ni un `allow` de agente ni un "Allow always" lo
levantan (falla con `Blocked by configuration policy`). Motivo: un subagente llegó
a volcar `~/.config/opencode/service.json` (contraseña en claro) al inventariar la
config. Si hay que rotar esa contraseña: `opencode service set password "…"` y
después `opencode service start` (cambiar un *setting* detiene el servidor de
fondo; la doc avisa de no editar los archivos de servicio a mano).

## 5. Cómo se verifica

```bash
bash ew_gui_tools/common/tests/test_agent_permissions.sh   # recetas + evasiones + forma
bash ew_gui_tools/common/tests/test_diag.sh                # contrato de diag.sh
make -C ew_gui_tools/common check                          # todo junto
```

`test_agent_permissions.sh` modela el matcher de la sección 1 (precedencia,
` *`, segmentos, `cd` relativo, comandos opacos) y ejecuta ~200 recetas: lo que
cada agente necesita para trabajar, y las evasiones que deben quedar denegadas
(`git -c … commit`, `PAGER=cat git log`, `sh -c "git push"`,
`git branch -a -d x`, `git diff --output=f`, `ls x > f`, `tee`, …).
Si alguien relaja una regla, el test lo dice con el comando exacto.

## 6. Limitaciones conocidas (a propósito)

- **Los prompts importan**: `build` y `runner` tienen `shell *` allow, así que su
  prohibición de git es de **flujo**, no una frontera de seguridad
  (`sh -c "git commit"` o un prefijo de entorno no son interceptables sin
  romperles la utilidad). En los agentes de solo lectura sí es una frontera:
  tienen catch-all `deny`, así que nada fuera de la lista puede ejecutarse.
- **`diag.sh` es granularidad gruesa**: el matcher no mira el contenido de un
  script, así que `sh …/diag.sh <sub>` vale por todos sus subcomandos. Por eso
  `diag.sh` implementa su propia lista cerrada (exit 3 fuera de ella) y por eso
  los agentes de solo lectura no pueden editarlo.
- **Un `ask` depende del cliente**: en una sesión interactiva muestra
  once/always/reject; en un cliente no interactivo se resuelve como denegación
  (por eso `plan` no puede "depender" de que le pregunten para completar un
  diagnóstico: si `diag.sh run` sale denegado, lo reporta y sigue).
