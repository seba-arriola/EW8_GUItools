# `db/` — Base de datos hipocentral (PostgreSQL)

Esquema y arranque de la base de datos que almacena la información hipocentral
del sistema (`event`, `origin`, `pick`, `magnitude`, `station_magnitude`,
`tensor`). El esquema es una **copia literal** del proyecto `test_db`
(simplificación QuakeML para el CSN); no se modifica aquí.

## Arranque (dev)

```bash
# 1) Credenciales: copiar el ejemplo una sola vez
cp run_working_v8/params/db.env.example run_working_v8/params/db.env

# 2) Levantar PostgreSQL 16 (puerto 5435, datos en un volumen docker)
docker compose -f db/docker-compose.yml up -d --wait

# 3) Verificar esquema (AC1)
db/test_schema.sh

# Detener / resetear
docker compose -f db/docker-compose.yml down        # detener (conserva datos)
docker compose -f db/docker-compose.yml down -v     # borrar datos y reaplicar esquema
```

El `schema.sql` se aplica **solo la primera vez** que se crea el volumen
(`/docker-entrypoint-initdb.d`). Tras editar `schema.sql` hay que hacer
`down -v` + `up` para reaplicarlo.

## Credenciales

Viven en `run_working_v8/params/db.env` (no versionado; versionado el
`.example`). Contiene:

- `DB_HOST/DB_PORT/DB_NAME/DB_USER/DB_PASSWORD` — conexión de los módulos (libpq).
- `POSTGRES_USER/POSTGRES_PASSWORD/POSTGRES_DB` — inicialización del contenedor
  (deben coincidir con las de arriba).

Las variables de entorno tienen precedencia sobre el archivo al conectar.

## Contenido

| Archivo | Rol |
|---|---|
| `schema.sql` | Esquema (copia literal de `test_db`) |
| `docker-compose.yml` | PostgreSQL 16 en `:5435` con el esquema autoaplicado |
| `test_schema.sh` | Verifica que el esquema crea las 6 tablas (AC1) |
