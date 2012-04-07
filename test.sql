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
	size INTEGER DEFAULT 0,
	data BYTEA
);

-- self-referencing anchor for root directory
INSERT INTO dir values( 0, 0, '/', '/', true );

