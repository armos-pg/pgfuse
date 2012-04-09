DROP RULE dir_insert ON dir;
DROP RULE dir_remove ON dir;
DROP TABLE data;
DROP TABLE dir;

CREATE TABLE dir (
	id SERIAL PRIMARY KEY,
	parent_id INTEGER REFERENCES dir( id ),
	name TEXT,
	path TEXT,
	isdir BOOL,
	UNIQUE( name, parent_id ),
	UNIQUE( path ),
	mode INTEGER NOT NULL DEFAULT 0,
	uid INTEGER NOT NULL DEFAULT 0,
	gid INTEGER NOT NULL DEFAULT 0,
	ctime TIMESTAMP WITH TIME ZONE,
	mtime TIMESTAMP WITH TIME ZONE,
	atime TIMESTAMP WITH TIME ZONE,
	size INTEGER DEFAULT 0
);

CREATE TABLE data (
	id INTEGER,
	FOREIGN KEY( id ) REFERENCES dir( id ),
	data BYTEA
);

CREATE INDEX data_id_idx ON data( id );

-- make sure file entries always get a data
-- section in the separate table
CREATE OR REPLACE RULE "dir_insert" AS ON
	INSERT TO dir WHERE NEW.isdir = false
	DO ALSO INSERT INTO data( id )
	VALUES ( currval( 'dir_id_seq' ) );

-- garbage collect deleted file entries
CREATE OR REPLACE RULE "dir_remove" AS ON
	DELETE TO dir WHERE OLD.isdir = false
	DO ALSO DELETE FROM data WHERE id=OLD.id;	
	
-- self-referencing anchor for root directory
INSERT INTO dir values( 0, 0, '/', '/', true );
