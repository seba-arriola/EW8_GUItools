#!/bin/bash
# test_agent_permissions - contrato de los permisos de los agentes de OpenCode.
#
# Por que: el permiso de shell se evalua POR SEGMENTO del comando (`a && b`,
# `a | b`, `a ; b` = varios recursos) y gana la ULTIMA regla que coincide. Con
# allowlists escritas "a ojo" eso produce dos fallos caros:
#   1. denegaciones constantes de comandos naturales (`git branch`, `| head`,
#      `source x && cmd`, `timeout N cmd`) -> el agente concluye "no tengo shell"
#      y se va por estrategias alternativas, gastando turnos;
#   2. agujeros silenciosos: `git branch -a -d x` o `git diff --output=f`
#      escriben aunque el agente sea "de solo lectura".
#
# Este test modela ese matcher (docs/AGENTES-PERMISOS.md) y verifica una tabla
# de recetas reales y de evasiones, mas invariantes de forma de la configuracion.
# Verificado contra el comportamiento medido en una sesion real (2026-10-14):
# `ls x 2>/dev/null` pasa, `ls x | head -3` y `LANG=C ls x` no, `cd . && ls`
# pasa, `cd /abs && ls` no, `ls x > f` pasaba (por eso hay denies de forma).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
AGENTS="$ROOT/.opencode/agents"
CONFIG="$ROOT/opencode.jsonc"
fail=0

ok() { echo "ok  : $1"; }
ko() { echo "FAIL: $1"; fail=1; }

TMP="$(mktemp -d)" || exit 1
trap 'rm -rf "$TMP"' EXIT

# --- modelo del matcher ------------------------------------------------------

parse_rules() {   # <archivo.md> -> action|resource|effect
    sed -n 's/^[[:space:]]*-[[:space:]]*{[[:space:]]*action:[[:space:]]*\([A-Za-z_.]*\),[[:space:]]*resource:[[:space:]]*"\{0,1\}\([^",]*\)"\{0,1\},[[:space:]]*effect:[[:space:]]*\([a-z]*\)[[:space:]]*}.*$/\1|\2|\3/p' "$1"
}

parse_global() {  # solo las reglas del array "permissions" de opencode.jsonc
    sed -n 's/^[[:space:]]*{[[:space:]]*"action":[[:space:]]*"\([A-Za-z_]*\)",[[:space:]]*"resource":[[:space:]]*"\([^"]*\)",[[:space:]]*"effect":[[:space:]]*"\([a-z]*\)".*$/\1|\2|\3/p' "$CONFIG"
}

effective_rules() {  # <agente>: las globales se anteponen, las del agente van al final
    parse_global
    [ -f "$AGENTS/$1.md" ] && parse_rules "$AGENTS/$1.md"
    return 0
}

glob_to_re() {    # * -> .*  ; ? -> .  ; el resto escapado
    # Regla documentada de V2: un patron que termina en " *" tambien casa sin
    # argumentos (`git diff *` cubre `git diff`).
    local p="$1" re
    re="$(printf '%s' "$p" | sed -e 's/[]\/$*.^[]/\\&/g' -e 's/\\\*/.*/g' -e 's/\\?/./g')"
    case "$p" in
        *" *")
            case "$re" in
                *" .*") printf '%s( .*)?' "${re%???}" ;;
                *)      printf '%s' "$re" ;;
            esac
            ;;
        *) printf '%s' "$re" ;;
    esac
}

build_cache() {   # <agente> -> $TMP/re_<agente> con "regex<TAB>effect"
    local ag="$1" act res eff
    : > "$TMP/re_$ag"
    while IFS='|' read -r act res eff; do
        [ "$act" = shell ] || continue
        printf '%s\t%s\n' "$(glob_to_re "$res")" "$eff" >> "$TMP/re_$ag"
    done < <(effective_rules "$ag")
}

rule_effect() {   # <agente> <segmento> -> ultima regla que coincide (o ask)
    local ag="$1" seg="$2" re eff found=""
    while IFS=$'\t' read -r re eff; do
        [[ "$seg" =~ ^$re$ ]] && found="$eff"
    done < "$TMP/re_$ag"
    printf '%s' "${found:-ask}"
}

segments() {      # divide el compuesto en sub-comandos (aproximacion sin comillas)
    # El `\n` del printf es imprescindible: sin el, `while read` ve EOF en la
    # PRIMERA iteracion (el ultimo registro no lleva salto) y el bucle no corre.
    printf '%s\n' "$1" | sed -e 's/&&/\n/g' -e 's/||/\n/g' -e 's/;/\n/g' -e 's/|/\n/g'
}

is_cd_rel() {     # `cd <ruta relativa>` es un cambio de directorio, no un comando
    case "$1" in
        cd\ /*) return 1 ;;
        cd\ ?*) return 0 ;;
        *)      return 1 ;;
    esac
}

effect_of() {     # <agente> <comando>: deny gana; si no, ask; si no, allow
    local ag="$1" cmd="$2" seg e result=allow
    while IFS= read -r seg; do
        [ -n "$seg" ] || continue
        seg="$(printf '%s' "$seg" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        # Un comando que el scanner no puede analizar (sustitucion de procesos,
        # de comandos o backticks) le llega como opaco y lo deniega: medido con
        # `diff <(echo a) <(echo b)`.
        case "$seg" in
            *'$('*|*'`'*|*'<('*|*'>('*) printf 'deny'; return 0 ;;
        esac
        is_cd_rel "$seg" && continue
        e="$(rule_effect "$ag" "$seg")"
        if [ "$e" = deny ]; then printf 'deny'; return 0; fi
        [ "$e" = ask ] && result=ask
    done < <(segments "$cmd")
    printf '%s' "$result"
}

check() {         # <agente> <efecto esperado> <comando>
    local ag="$1" want="$2" cmd="$3" got
    got="$(effect_of "$ag" "$cmd")"
    if [ "$got" = "$want" ]; then
        ok "[$ag] $want  ← $cmd"
    else
        ko "[$ag] '$cmd': el modelo dice $got y se esperaba $want"
    fi
}

# --- cache y presencia de agentes -------------------------------------------
for ag in plan build explore runner scout review; do
    [ -f "$AGENTS/$ag.md" ] || { ko "falta el agente $ag.md"; continue; }
    build_cache "$ag"
done

# --- recetas: lo que cada agente necesita para su trabajo --------------------
while IFS= read -r line; do
    [ -n "$line" ] || continue
    ag="${line%%@@*}"; rest="${line#*@@}"; want="${rest%%@@*}"; cmd="${rest#*@@}"
    check "$ag" "$want" "$cmd"
done <<'RECIPES'
plan@@allow@@ls -1 .opencode
plan@@allow@@ls -1 .opencode 2>/dev/null
plan@@allow@@ls -1 a; ls -1 b
plan@@allow@@cd run_working_v8/params && ls -1 csnloc.d
plan@@allow@@git status --short
plan@@allow@@git diff
plan@@allow@@git diff --stat
plan@@allow@@git log --oneline -3
plan@@allow@@git show HEAD
plan@@allow@@git rev-parse HEAD
plan@@allow@@git merge-base master HEAD
plan@@allow@@git branch --show-current
plan@@allow@@git branch -a
plan@@allow@@git branch --list
plan@@allow@@git remote -v
plan@@allow@@sh ew_gui_tools/common/tools/diag.sh rings
plan@@allow@@sh ew_gui_tools/common/tools/diag.sh process
plan@@allow@@sh ew_gui_tools/common/tools/diag.sh runx csnloc
plan@@ask@@sh ew_gui_tools/common/tools/diag.sh run csnloc 20
plan@@ask@@bash ew_gui_tools/common/tools/diag.sh run --bg csnhypodbp 30
plan@@deny@@echo hola
plan@@deny@@cat opencode.jsonc
plan@@deny@@find . -name x
plan@@deny@@source ./ew8_unix.sh
plan@@deny@@/usr/bin/timeout 5 ls -1 .
plan@@deny@@LANG=C ls -1 .
plan@@deny@@ls -1 . | head -3
plan@@deny@@ls -1 . > /tmp/opencode/x
plan@@deny@@ls -1 . >> /tmp/opencode/x
plan@@deny@@tee /tmp/opencode/x
plan@@deny@@sed -i s/a/b/ f
plan@@deny@@make -C ew_gui_tools/common
plan@@deny@@cd /tmp && ls -1 .
plan@@deny@@git add -A
plan@@deny@@git commit -m x
plan@@deny@@git push
plan@@deny@@git branch -d foo
plan@@deny@@git branch foo
plan@@deny@@git branch -a -d foo
plan@@deny@@git diff --stat --output=/tmp/opencode/x
plan@@deny@@git log --ext-diff
plan@@deny@@git remote add origin x
plan@@deny@@git remote -v remove origin
plan@@deny@@git --no-pager log
plan@@deny@@git -c core.pager=cat log
plan@@deny@@ew_monitor.sh start
plan@@deny@@startstop
explore@@allow@@ls -1 x 2>/dev/null
explore@@allow@@wc -l opencode.jsonc
explore@@allow@@grep -n effect f | head -3
explore@@allow@@cd . && ls -1 x
explore@@allow@@git log --oneline -3
explore@@allow@@git branch --show-current
explore@@allow@@git rev-parse HEAD
explore@@allow@@git merge-base master HEAD
explore@@allow@@git remote -v
explore@@allow@@git describe --tags
explore@@allow@@git tag -l
explore@@allow@@git stash list
explore@@allow@@git worktree list
explore@@deny@@cat f
explore@@deny@@find . -name x
explore@@deny@@ls -1 x > f
explore@@deny@@diff <(echo a) <(echo b)
explore@@deny@@/usr/bin/timeout 5 ls x
explore@@deny@@VAR=1 ls x
explore@@deny@@cd /tmp && ls x
explore@@deny@@sort -o /tmp/opencode/x f
explore@@deny@@git push
explore@@deny@@git branch -d foo
explore@@deny@@git tag v1
explore@@deny@@git tag -l -d v1
explore@@deny@@git stash pop
explore@@deny@@git worktree add /tmp/x
explore@@deny@@git config --global user.name x
explore@@deny@@git --no-pager log
explore@@deny@@git -c core.pager=cat log
explore@@deny@@git grep --open-files-in-pager=less x
explore@@deny@@git grep -Oless x
review@@allow@@git status --short
review@@allow@@git log --oneline master..rama
review@@allow@@git diff master...rama
review@@allow@@git diff --stat master...rama
review@@allow@@git diff --check
review@@allow@@git show --stat HEAD
review@@allow@@git rev-parse HEAD
review@@allow@@git merge-base master rama
review@@allow@@git rev-list --count master..rama
review@@allow@@git ls-files
review@@allow@@git cat-file -p HEAD
review@@allow@@git for-each-ref --format=%(refname:short)
review@@allow@@git shortlog -sn
review@@allow@@git branch --show-current
review@@allow@@git branch -a
review@@allow@@git remote -v
review@@allow@@git remote show origin
review@@allow@@git tag -l
review@@allow@@git stash list
review@@allow@@git worktree list
review@@deny@@git add -A
review@@deny@@git add .
review@@deny@@git commit -m x
review@@deny@@git commit --amend -m x
review@@deny@@git push
review@@deny@@git push origin master
review@@deny@@git pull
review@@deny@@git fetch
review@@deny@@git checkout main
review@@deny@@git switch main
review@@deny@@git restore .
review@@deny@@git reset --hard
review@@deny@@git merge rama
review@@deny@@git rebase master
review@@deny@@git cherry-pick abc123
review@@deny@@git revert HEAD
review@@deny@@git apply x.patch
review@@deny@@git am x.mbox
review@@deny@@git rm x
review@@deny@@git mv a b
review@@deny@@git clean -fdx
review@@deny@@git stash
review@@deny@@git stash pop
review@@deny@@git stash drop
review@@deny@@git tag v1
review@@deny@@git tag -d v1
review@@deny@@git branch nueva
review@@deny@@git branch -D master
review@@deny@@git branch -m x
review@@deny@@git branch -a -D master
review@@deny@@git remote add origin x
review@@deny@@git remote remove origin
review@@deny@@git remote set-url origin x
review@@deny@@git remote prune origin
review@@deny@@git remote -v remove origin
review@@deny@@git worktree add /tmp/x
review@@deny@@git worktree remove /tmp/x
review@@deny@@git config --global user.name x
review@@deny@@git update-ref refs/heads/x y
review@@deny@@git notes add -m x
review@@deny@@git submodule add x
review@@deny@@git gc
review@@deny@@git archive HEAD
review@@deny@@git --no-pager push
review@@deny@@git -c core.pager=cat log
review@@deny@@git diff --output=/tmp/opencode/x
review@@deny@@git diff --stat --output=/tmp/opencode/x
review@@deny@@git diff --ext-diff
review@@deny@@git log --ext-diff
review@@deny@@PAGER=cat git log
review@@deny@@sh -c "git push"
review@@deny@@ls -1 . > /tmp/opencode/x
review@@deny@@tee /tmp/opencode/x
review@@deny@@make -C ew_gui_tools/common
review@@deny@@startstop
review@@deny@@ew_monitor.sh start
review@@deny@@sudo systemctl stop earthworm
review@@deny@@kill 1234
review@@deny@@cat opencode.jsonc
build@@allow@@ls -1 .opencode
build@@allow@@git status --short
build@@allow@@git diff
build@@allow@@make -C ew_gui_tools/common
build@@deny@@git add -A
build@@deny@@git commit -m x
build@@deny@@git push
build@@deny@@git -c user.name=x commit -m y
build@@deny@@git --no-pager push
build@@deny@@sudo rm x
build@@deny@@rm -rf /tmp/opencode/x
build@@ask@@startstop
build@@ask@@./ew_monitor.sh start
runner@@allow@@ls -1 run_working_v8/log
runner@@allow@@make -C ew_gui_tools/common
runner@@deny@@git add -A
runner@@deny@@git commit -m x
runner@@deny@@git push
runner@@deny@@git reset --hard
runner@@deny@@git clean -fdx
runner@@deny@@startstop
runner@@deny@@./ew_monitor.sh stop
runner@@deny@@sudo x
runner@@deny@@rm -rf x
RECIPES

# --- invariantes de forma de la configuracion --------------------------------
[ -d "$AGENTS" ] && ok "los agentes viven en .opencode/agents (ruta canonica V2)" \
                 || ko "no existe $AGENTS"
[ -e "$ROOT/.opencode/agent" ] && ko "sigue existiendo la ruta singular .opencode/agent" \
                               || ok "no queda la ruta singular .opencode/agent"
if ls "$ROOT/.opencode/"*.gz >/dev/null 2>&1; then
    ko "hay un artefacto .gz suelto en .opencode/"
else
    ok "sin artefactos .gz en .opencode/"
fi

incompletos=0
for f in "$AGENTS"/*.md; do
    n="$(basename "$f" .md)"
    grep -q '^description:' "$f" || { ko "$n: sin 'description'"; incompletos=1; }
    grep -q '^mode:'        "$f" || { ko "$n: sin 'mode'"; incompletos=1; }
    grep -q '^permissions:' "$f" || { ko "$n: sin 'permissions'"; incompletos=1; }
    parse_rules "$f" | grep -q '^edit|' || { ko "$n: no declara el permiso 'edit'"; incompletos=1; }
done
[ "$incompletos" = 0 ] && ok "todos los agentes declaran description/mode/permissions/edit"

# Un `shell *` allow de agente BORRA los deny globales (se anteponen): quien lo
# use tiene que volver a declararlos despues, o el deny desaparece en silencio.
for f in "$AGENTS"/*.md; do
    n="$(basename "$f" .md)"
    parse_rules "$f" | grep -qF 'shell|*|allow' || continue
    rules="$(parse_rules "$f")"
    for r in 'sudo *' 'rm -rf *' 'rm -fr *' 'git add *' 'git commit *' 'git push *'; do
        if printf '%s\n' "$rules" | grep -qF "shell|$r|deny"; then
            :
        else
            ko "$n tiene 'shell *' allow y no re-declara el deny '$r' (el global queda anulado)"
        fi
    done
done
ok "los agentes con 'shell *' allow re-declaran todos los deny globales"

# Ningun agente de lectura puede editar ni tocar git de escritura.
for ag in plan explore review; do
    if parse_rules "$AGENTS/$ag.md" | grep -qE '^edit\|[^|]*\|allow'; then
        ko "$ag puede editar archivos"
    else
        ok "$ag no tiene 'edit' permitido"
    fi
done
if parse_rules "$AGENTS/plan.md" | grep -qF 'read|*|deny'; then
    ok "plan sigue sin leer archivos (delega en @explore)"
else
    ko "plan recobro 'read': se rompe la delegacion obligatoria"
fi
if parse_rules "$AGENTS/review.md" | grep -qF 'subagent|*|deny'; then
    ok "review no puede lanzar subagentes (no puede lavar una escritura)"
else
    ko "review puede lanzar subagentes: una escritura podria esconderse ahi"
fi

# Policies: credenciales fuera del alcance de cualquier agente.
grep -qF '"policies"' "$CONFIG" || ko "opencode.jsonc no declara el bloque policies"
grep -qF 'read:*service.json' "$CONFIG" && ok "policy: service.json no es legible por agentes" \
                                        || ko "falta la policy de service.json"
grep -qF 'read:*db.env' "$CONFIG" && ok "policy: db.env no es legible por agentes" \
                                  || ko "falta la policy de db.env"

# Superficie documentada.
[ -f "$ROOT/docs/AGENTES-PERMISOS.md" ] && ok "existe docs/AGENTES-PERMISOS.md" \
                                        || ko "falta docs/AGENTES-PERMISOS.md"
grep -qF 'diag.sh' "$ROOT/AGENTS.md" && ok "AGENTS.md referencia diag.sh" \
                                     || ko "AGENTS.md no menciona diag.sh"

if [ "$fail" -gt 0 ]; then echo; echo "$fail FALLOS"; exit 1; fi
echo; echo "OK test_agent_permissions"
