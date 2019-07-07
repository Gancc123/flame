#!/bin/bash

DBNAME="flame_mgr_db"
PASSWORD="123456"
use_db_sql="use ${DBNAME};"
drop_sql="drop table chunk, chunk_health, csd, csd_health, gateway, volume, volume_group, cluster;"

a=`ps aux | grep csd | awk '{print $2}' | sed -n '1p'`
kill -9 $a
kill -9 $a
a=`ps aux | grep mgr | awk '{print $2}' | sed -n '1p'`
kill -9 $a

sleep 5
mysql -p${PASSWORD} -e "${use_db_sql} ${drop_sql}"
./flame-sp/build/bin/mgr >mgr.log 2>&1 &
./flame-sp/build/bin/csd -c /etc/flame/csd.conf  --log_level TRACE --force_format -r 0xf --rpc_addr /var/temp/spdk_csd.sock -f /etc/flame/nvme.conf >csd.log >&1 &
sleep 4
./flame-sp/build/bin/csd -c /etc/flame/csd2.conf  --log_level TRACE --force_format -r 0xf0 --rpc_addr /var/temp/spdk_csd2.sock -f /etc/flame/nvme2.conf >csd2.log 2>&1 &