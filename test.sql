DROP TABLE dir;
DROP TABLE inode;
DROP TABLE data;

CREATE TABLE data (
	id SERIAL PRIMARY KEY,
	data BYTEA
);

CREATE TABLE inode (
	id SERIAL PRIMARY KEY,
	data_id INTEGER REFERENCES data( id ),
	mode INTEGER NOT NULL DEFAULT 0,
	uid INTEGER NOT NULL DEFAULT 0,
	gid INTEGER NOT NULL DEFAULT 0,
	ctime TIMESTAMP WITH TIME ZONE,
	mtime TIMESTAMP WITH TIME ZONE,
	atime TIMESTAMP WITH TIME ZONE,
	size INTEGER DEFAULT 0	
);

CREATE TABLE dir (
	id SERIAL PRIMARY KEY,
	parent_id INTEGER REFERENCES dir( id ),
	name TEXT,
	path TEXT,
	isdir BOOL,
	UNIQUE( name, parent_id ),
	UNIQUE( path )
);

-- empty data field for indoes without data
-- e.g. directories
INSERT INTO data(data) values( '' );

-- self-referencing anchor for root directory
INSERT INTO dir values( 0, 0, '/', '/', true );

