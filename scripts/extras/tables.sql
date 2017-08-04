CREATE TABLE users (
	id SERIAL PRIMARY KEY,
	username VARCHAR(50) UNIQUE NOT NULL,
	password TEXT NOT NULL,
	primary_token TEXT,
	secondary_token TEXT,
	email VARCHAR(100) UNIQUE NOT NULL,
	first_name VARCHAR(50) NOT NULL,
	last_name VARCHAR(50) NOT NULL,
	verified BOOLEAN NOT NULL,
	created_on TIMESTAMP NOT NULL
);
ALTER TABLE users ALTER verified SET DEFAULT FALSE;
ALTER TABLE users ALTER created_on SET DEFAULT now();
CREATE INDEX ON users (username);
CREATE INDEX ON users (email);
CREATE INDEX ON users (created_on);

CREATE EXTENSION postgis;
CREATE TABLE poi (
	id SERIAL PRIMARY KEY,
	owner_id SERIAL NOT NULL,
	label TEXT NOT NULL,
	description TEXT NOT NULL,
	location GEOGRAPHY(POINT, 4326) NOT NULL,
	created_on TIMESTAMP NOT NULL
);
ALTER TABLE poi ALTER created_on SET DEFAULT now();
CREATE INDEX ON poi (owner_id);
CREATE INDEX ON poi (created_on);
ALTER TABLE poi ADD CONSTRAINT fk_owner_id FOREIGN KEY (owner_id) REFERENCES users(id);

CREATE TABLE tags (
	id SERIAL PRIMARY KEY,
	tag VARCHAR(25) UNIQUE NOT NULL
);
CREATE INDEX ON tags (tag);

DROP TABLE accessors;
CREATE TABLE accessors (
	id SERIAL PRIMARY KEY,
	access VARCHAR(25) NOT NULL,
	description TEXT NOT NULL
);
INSERT INTO accessors VALUES (0, 'public read', 'Anyone can read the associated object.');
INSERT INTO accessors VALUES (1, 'public read and write', 'Anyone can read and write the associated object.');

CREATE TABLE threads (
	id SERIAL PRIMARY KEY,
	owner_id SERIAL NOT NULL,
	access_id SERIAL NOT NULL,
	name VARCHAR(50) UNIQUE NOT NULL,
	created_on TIMESTAMP NOT NULL
);
ALTER TABLE threads ALTER created_on SET DEFAULT now();
CREATE INDEX ON threads (created_on);
CREATE INDEX ON threads (owner_id);
CREATE INDEX ON threads (access_id);
CREATE INDEX ON threads (created_on);
ALTER TABLE threads ADD CONSTRAINT fk_owner_id FOREIGN KEY (owner_id) REFERENCES users(id);
ALTER TABLE threads ADD CONSTRAINT fk_access_id FOREIGN KEY (access_id) REFERENCES accessors(id);

CREATE TABLE thread_tags (
	thread_id SERIAL NOT NULL,
	tag_id SERIAL NOT NULL,
	PRIMARY KEY (thread_id, tag_id)
);
CREATE INDEX ON thread_tags (thread_id);
CREATE INDEX ON thread_tags (tag_id);
ALTER TABLE thread_tags ADD CONSTRAINT fk_thread_id FOREIGN KEY (thread_id) REFERENCES threads(id);
ALTER TABLE thread_tags ADD CONSTRAINT fk_tag_id FOREIGN KEY (tag_id) REFERENCES tags(id);

CREATE TABLE posts (
	id SERIAL PRIMARY KEY,
	owner_id SERIAL NOT NULL,
	thread_id SERIAL NOT NULL,
	title TEXT NOT NULL,
	content TEXT NOT NULL,
	created_on TIMESTAMP NOT NULL
);
ALTER TABLE posts ALTER created_on SET DEFAULT now();
CREATE INDEX ON posts (owner_id);
CREATE INDEX ON posts (thread_id);
CREATE INDEX ON posts (created_on);
ALTER TABLE posts ADD CONSTRAINT fk_owner_id FOREIGN KEY (owner_id) REFERENCES users(id);
ALTER TABLE posts ADD CONSTRAINT fk_thread_id FOREIGN KEY (thread_id) REFERENCES threads(id);

CREATE ROLE bwackwat WITH LOGIN;
ALTER ROLE bwackwat PASSWORD 'abc123';
GRANT SELECT, INSERT, UPDATE ON users TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON poi TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON tags TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON accessors TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON threads TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON thread_tags TO bwackwat;
GRANT SELECT, INSERT, UPDATE ON posts TO bwackwat;

GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO bwackwat;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO bwackwat;
