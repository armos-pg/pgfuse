#!/bin/sh

VERSION=0.0.1
OSC_HOME=$HOME/home:andreas_baumann/pgfuse

rm -f wolframe-$VERSION.tar.gz
make \
	WITH_SSL=1 WITH_EXPECT=1 WITH_QT=1 WITH_PAM=1 WITH_SASL=1 \
	WITH_SQLITE3=1 WITH_PGSQL=1 WITH_LUA=1 WITH_LIBXML2=1 WITH_LIBXSLT=1 \
	dist-gz
cp wolframe-$VERSION.tar.gz $OSC_HOME/wolframe_$VERSION.tar.gz
cp redhat/wolframe.spec $OSC_HOME/wolframe.spec

SIZE=`stat -c '%s' $OSC_HOME/wolframe_$VERSION.tar.gz`
CHKSUM=`md5sum $OSC_HOME/wolframe_$VERSION.tar.gz  | cut -f 1 -d' '`

cat contrib/osc/wolframe.dsc > $OSC_HOME/wolframe.dsc
echo " $CHKSUM $SIZE wolframe_$VERSION.tar.gz" >> $OSC_HOME/wolframe.dsc
