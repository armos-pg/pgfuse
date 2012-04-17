CREATE TABLE dir (
	id SERIAL PRIMARY KEY,
	parent_id INTEGER REFERENCES dir( id ),
	name TEXT,
	path TEXT,
	UNIQUE( name, parent_id ),
	UNIQUE( path ),
	size INTEGER DEFAULT 0,
	mode INTEGER NOT NULL DEFAULT 0,
	uid INTEGER NOT NULL DEFAULT 0,
	gid INTEGER NOT NULL DEFAULT 0,
	inuse BOOL DEFAULT false,
	ctime TIMESTAMP,
	mtime TIMESTAMP,
	atime TIMESTAMP
);

CREATE TABLE data (
	id INTEGER,
	FOREIGN KEY( id ) REFERENCES dir( id ),
	data BYTEA
);

-- create an index for fast data access
CREATE INDEX data_id_idx ON data( id );

-- create an index on the parent_id for
-- directory listings
CREATE INDEX dir_parent_id_idx ON dir( parent_id );

-- 16384 == S_IFDIR (S_IFDIR)
-- TODO: should be created by the program after checking the OS
-- it is running on (for full POSIX compatibility)

-- make sure file entries always get a data
-- section in the separate table
CREATE OR REPLACE RULE "dir_insert" AS ON
	INSERT TO dir WHERE NEW.mode & 16384 = 0
	DO ALSO INSERT INTO data( id )
	VALUES ( currval( 'dir_id_seq' ) );

-- garbage collect deleted file entries
CREATE OR REPLACE RULE "dir_remove" AS ON
	DELETE TO dir WHERE OLD.mode & 16384 = 0
	DO ALSO DELETE FROM data WHERE id=OLD.id;	
	
-- self-referencing anchor for root directory
-- 16895 = S_IFDIR and 0777 permissions
-- TODO: should be done from outside, see note above
INSERT INTO dir( id, parent_id, name, path, size, mode, uid, gid, ctime, mtime, atime )
	VALUES( 0, 0, '/', '/', 0, 16895, 0, 0, NOW( ), NOW( ), NOW( ) );
