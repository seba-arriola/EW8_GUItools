-- =============================================================================
-- Seismological Database Schema (PostgreSQL)
-- Based on a simplification of the QuakeML model for the National Seismological Center (CSN)
-- Sebastian Arriola sarriola@csn.uchile.cl seba.arriola.s@gmail.com
-- =============================================================================

-- Useful extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- -----------------------------------------------------------------------------
-- Table: event
-- Represents the main earthquake entity
-- -----------------------------------------------------------------------------
CREATE TABLE event (
    event_id              VARCHAR(64) PRIMARY KEY,
    creation_time         TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    event_type            VARCHAR(32) NOT NULL DEFAULT 'EARTHQUAKE',
    felt                  BOOLEAN NOT NULL DEFAULT FALSE,
    published             BOOLEAN NOT NULL DEFAULT FALSE,
    preferred_origin_id   VARCHAR(64) NULL,
    preferred_magnitude_id VARCHAR(64) NULL
);

COMMENT ON TABLE event IS 'Main table grouping different solutions and observations of a seismic event.';
COMMENT ON COLUMN event.event_id IS 'Unique event identifier (e.g., csn2026abcd).';
COMMENT ON COLUMN event.event_type IS 'Type of event: EARTHQUAKE, EXPLOSION, etc.';
COMMENT ON COLUMN event.felt IS 'Indicates whether the earthquake was reported as felt.';
COMMENT ON COLUMN event.published IS 'Indicates whether the event has been published on the website (TRUE) or not (FALSE).';
COMMENT ON COLUMN event.preferred_origin_id IS 'Optional foreign key to indicate the preferred hypocentral solution.';
COMMENT ON COLUMN event.preferred_magnitude_id IS 'Optional foreign key to indicate the preferred magnitude of the event.';

-- -----------------------------------------------------------------------------
-- Table: origin
-- Stores hypocenter and location solutions (preliminary, revised, final)
-- -----------------------------------------------------------------------------
CREATE TABLE origin (
    origin_id             VARCHAR(64) PRIMARY KEY,
    event_id              VARCHAR(64) NOT NULL REFERENCES event(event_id) ON DELETE CASCADE,
    origin_time           TIMESTAMPTZ NOT NULL,
    latitude              DOUBLE PRECISION NOT NULL,
    longitude             DOUBLE PRECISION NOT NULL,
    depth                 DOUBLE PRECISION NOT NULL,
    author                VARCHAR(64),
    software              VARCHAR(64),
    creation_time         TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    location_algorithm    VARCHAR(64),
    velocity_model        VARCHAR(64),
    azimuthal_gap         DOUBLE PRECISION,
    latitude_uncertainty  DOUBLE PRECISION,
    longitude_uncertainty DOUBLE PRECISION,
    depth_uncertainty     DOUBLE PRECISION,
    time_uncertainty      DOUBLE PRECISION,
    error_ellipse         VARCHAR(255),
    station_count         INTEGER,
    standard_error        DOUBLE PRECISION,
    status                VARCHAR(32) NOT NULL DEFAULT 'PRELIMINARY'
);

COMMENT ON TABLE origin IS 'Hypocentral solutions associated with an event in different review stages.';
COMMENT ON COLUMN origin.latitude IS 'Hypocenter latitude in decimal degrees (-90 to 90).';
COMMENT ON COLUMN origin.longitude IS 'Hypocenter longitude in decimal degrees (-180 to 180).';
COMMENT ON COLUMN origin.depth IS 'Focal depth in kilometers (km).';
COMMENT ON COLUMN origin.depth_uncertainty IS 'Focal depth error in kilometers (km).';
COMMENT ON COLUMN origin.time_uncertainty IS 'Origin time error in seconds.';
COMMENT ON COLUMN origin.standard_error IS 'RMS residual of the location solution (seconds).';
COMMENT ON COLUMN origin.status IS 'Review stage: PRELIMINARY, REVISED 1-3, CONFIRMED, FINAL.';

-- -----------------------------------------------------------------------------
-- Table: magnitude
-- Stores calculated magnitudes associated with a specific origin solution
-- -----------------------------------------------------------------------------
CREATE TABLE magnitude (
    magnitude_id       VARCHAR(64) PRIMARY KEY,
    origin_id          VARCHAR(64) NOT NULL REFERENCES origin(origin_id) ON DELETE CASCADE,
    magnitude_value    DOUBLE PRECISION NOT NULL,
    magnitude_type     VARCHAR(16) NOT NULL,
    mag_uncertainty    DOUBLE PRECISION,
    calculation_method VARCHAR(64),
    station_count      INTEGER
);

COMMENT ON TABLE magnitude IS 'Magnitude calculations linked to a specific hypocentral solution.';
COMMENT ON COLUMN magnitude.magnitude_type IS 'Magnitude scale: Mw, ML, Mb, Ms, Mww, etc.';
COMMENT ON COLUMN magnitude.mag_uncertainty IS 'Magnitude error (dimensionless). Not available in CSN SFILEs; only populated from external sources such as the RabbitMQ queue.';

-- -----------------------------------------------------------------------------
-- Table: station_magnitude
-- Readings and magnitudes calculated at each individual station/channel level
-- -----------------------------------------------------------------------------
CREATE TABLE station_magnitude (
    station_magnitude_id VARCHAR(64) PRIMARY KEY,
    magnitude_id         VARCHAR(64) NOT NULL REFERENCES magnitude(magnitude_id) ON DELETE CASCADE,
    channel              VARCHAR(64) NOT NULL,
    read_amplitude       DOUBLE PRECISION,
    calculated_magnitude DOUBLE PRECISION NULL,
    amplitude_type       VARCHAR(16)
);

COMMENT ON TABLE station_magnitude IS 'Individual station contribution to the global magnitude.';
COMMENT ON COLUMN station_magnitude.channel IS 'NSLCO Naming code (e.g., C1.GO01..HHZ).';
COMMENT ON COLUMN station_magnitude.amplitude_type IS 'SEISAN amplitude phase code (IAML, AML, IAmb, AMb, AMS, IVMs, IVmB, AMP).';

-- -----------------------------------------------------------------------------
-- Table: tensor
-- Moment tensor and focal mechanisms calculated for an origin
-- -----------------------------------------------------------------------------
CREATE TABLE tensor (
    tensor_id       VARCHAR(64) PRIMARY KEY,
    origin_id       VARCHAR(64) NOT NULL REFERENCES origin(origin_id) ON DELETE CASCADE,
    method          VARCHAR(64),
    mechanism       VARCHAR(64),
    -- Nodal Plane 1
    strike1         DOUBLE PRECISION,
    dip1            DOUBLE PRECISION,
    rake1           DOUBLE PRECISION,
    -- Nodal Plane 2
    strike2         DOUBLE PRECISION,
    dip2            DOUBLE PRECISION,
    rake2           DOUBLE PRECISION,
    -- Moment Tensor Components (Nm or dyne-cm)
    m_rr            DOUBLE PRECISION,
    m_tt            DOUBLE PRECISION,
    m_pp            DOUBLE PRECISION,
    m_rt            DOUBLE PRECISION,
    m_rp            DOUBLE PRECISION,
    m_tp            DOUBLE PRECISION,
    scalar_moment   DOUBLE PRECISION
);

COMMENT ON TABLE tensor IS 'Focal mechanism and seismic moment tensor components.';

-- -----------------------------------------------------------------------------
-- Table: pick
-- Arrival times (picks) associated directly with an origin solution
-- -----------------------------------------------------------------------------
CREATE TABLE pick (
    pick_id       VARCHAR(64) PRIMARY KEY,
    origin_id     VARCHAR(64) NOT NULL REFERENCES origin(origin_id) ON DELETE CASCADE,
    channel       VARCHAR(64) NOT NULL,
    pick_time     TIMESTAMPTZ NOT NULL,
    phase         VARCHAR(16) NOT NULL,
    polarity      VARCHAR(16),
    uncertainty   DOUBLE PRECISION,
    probability   DOUBLE PRECISION,
    weight        DOUBLE PRECISION NOT NULL DEFAULT 1.0,
    pick_status   VARCHAR(16) NOT NULL DEFAULT 'automatic'
);

COMMENT ON TABLE pick IS 'Phase arrival time readings (P, S) associated with an origin solution.';
COMMENT ON COLUMN pick.channel IS 'NSLCO Naming code (e.g., C1.GO01..HHZ).';
COMMENT ON COLUMN pick.phase IS 'Identified seismic phase (P, S, Pg, Sg, etc.).';
COMMENT ON COLUMN pick.polarity IS 'First motion polarity: UP (C), DOWN (D), UNDEF.';
COMMENT ON COLUMN pick.uncertainty IS 'Estimated arrival time error in seconds.';
COMMENT ON COLUMN pick.probability IS 'Probability assigned by AI picker (0.0 to 1.0).';
COMMENT ON COLUMN pick.weight IS 'Weighting factor of the arrival (0.0 unused to 1.0 full weight).';
COMMENT ON COLUMN pick.pick_status IS 'Status of the pick: automatic or manual.';

-- -----------------------------------------------------------------------------
-- Constraints on event (deferred foreign keys)
-- The event <-> origin relationship is circular (event.preferred_origin_id
-- references origin while origin.event_id references event), so these foreign
-- keys cannot be declared inline in CREATE TABLE event and are added here,
-- after every table exists.
-- -----------------------------------------------------------------------------
ALTER TABLE event
    ADD CONSTRAINT fk_event_preferred_origin
    FOREIGN KEY (preferred_origin_id) REFERENCES origin(origin_id)
    ON DELETE SET NULL
    DEFERRABLE INITIALLY DEFERRED;

ALTER TABLE event
    ADD CONSTRAINT fk_event_preferred_magnitude
    FOREIGN KEY (preferred_magnitude_id) REFERENCES magnitude(magnitude_id)
    ON DELETE SET NULL
    DEFERRABLE INITIALLY DEFERRED;

-- =============================================================================
-- Indexes to optimize frequent queries
-- =============================================================================

CREATE INDEX idx_origin_event ON origin(event_id);
CREATE INDEX idx_origin_time ON origin(origin_time);
CREATE INDEX idx_magnitude_origin ON magnitude(origin_id);
CREATE INDEX idx_station_magnitude_magnitude ON station_magnitude(magnitude_id);
CREATE INDEX idx_tensor_origin ON tensor(origin_id);
CREATE INDEX idx_picks_origin ON pick(origin_id);
CREATE INDEX idx_picks_time ON pick(pick_time);

-- Índices agregados para el visualizador de hipocentros (intranet CSN):
-- joins event→origin/magnitude preferidos y filtros de bbox/profundidad/orden.
CREATE INDEX idx_event_preferred_origin ON event(preferred_origin_id);
CREATE INDEX idx_event_preferred_magnitude ON event(preferred_magnitude_id);
CREATE INDEX idx_origin_lat_lon ON origin(latitude, longitude);
CREATE INDEX idx_origin_depth ON origin(depth);
CREATE INDEX idx_magnitude_value ON magnitude(magnitude_value);
