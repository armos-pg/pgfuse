CREATE LANGUAGE plperlu;
CREATE OR REPLACE FUNCTION db_disk_free()
  RETURNS bigint AS
$BODY$
# Get size of data_directory partition
# this uses UNIX df(1) command

# reported by df(1)
my $df=`df -B1 -a -P \$(psql -c "show data_directory;" -t)`;
my @df=split(/[\n\r]+/,$df);
shift @df;
for my $l (@df) { 
   my @a=split(/\s+/,$l);
   return $a[3];
}
 
return undef;
$BODY$
  LANGUAGE 'plperlu' VOLATILE
  COST 1000;
COMMENT ON FUNCTION db_disk_free() IS 'Get free disck space of data_directory partition';
