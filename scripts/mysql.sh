#!/bin/bash

DBNAME="flame_mgr_db"
PASSWORD="123456"
USER_NAME="flame"
if_not_exist_create_database="create database if not exists ${DBNAME}"
if_not_exist_create_user="create user if not exists '${USER_NAME}'@'localhost' identified by '123456'"
grant_privileges="grant all privileges on ${DBNAME}.* to '${USER_NAME}'@'localhost' identified by '123456'"
flush_privileges="flush privileges"

mysql -p${PASSWORD} -e "${if_not_exist_create_database}"
mysql -p${PASSWORD} -e "${if_not_exist_create_user}"
mysql -p${PASSWORD} -e "${grant_privileges}"
mysql -p${PASSWORD} -e "${flush_privileges}"
