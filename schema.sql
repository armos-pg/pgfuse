CREATE TABLE dir (
	id BIGSERIAL,
	parent_id BIGINT,
	name TEXT,
	size BIGINT DEFAULT 0,
	mode INTEGER NOT NULL DEFAULT 0,
	uid INTEGER NOT NULL DEFAULT 0,
	gid INTEGER NOT NULL DEFAULT 0,
	ctime TIMESTAMP,
	mtime TIMESTAMP,
	atime TIMESTAMP,
	PRIMARY KEY( id ),
	FOREIGN KEY( parent_id ) REFERENCES dir( id ),
	UNIQUE( name, parent_id )
);

-- TODO: 4096 is STANDARD_BLOCK_SIZE in config.h, must be in sync!
CREATE TABLE data (
	dir_id BIGINT,
	block_no BIGINT NOT NULL DEFAULT 0,
	data BYTEA,
	PRIMARY KEY( dir_id, block_no ),
	FOREIGN KEY( dir_id ) REFERENCES dir( id )
);

-- create indexes for fast data access
CREATE INDEX data_dir_id_idx ON data( dir_id );
CREATE INDEX data_block_no_idx ON data( block_no );

-- create an index on the parent_id for
-- directory listings
CREATE INDEX dir_parent_id_idx ON dir( parent_id );

-- 16384 == S_IFDIR (S_IFDIR)
-- TODO: should be created by the program after checking the OS
-- it is running on (for full POSIX compatibility)

-- garbage collect deleted file entries, delete all blocks in 'data'
CREATE OR REPLACE RULE "dir_remove" AS ON
	DELETE TO dir WHERE OLD.mode & 16384 = 0
	DO ALSO DELETE FROM data WHERE dir_id=OLD.id;	
	
-- self-referencing anchor for root directory
-- 16895 = S_IFDIR and 0777 permissions, belonging to root/root
-- TODO: should be done from outside, see note above
INSERT INTO dir( id, parent_id, name, size, mode, uid, gid, ctime, mtime, atime )
	VALUES( 0, 0, '/', 0, 16895, 0, 0, NOW( ), NOW( ), NOW( ) );
