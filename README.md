# FilecodeWebServer

## 部署操作

### （1）创建数据库 filecodeserver
CREATE DATABASE filecodeserver;

### （2）使用数据库
use filecodeserver


### （3）创建相关表7个
CREATE TABLE `DHF`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`stage` VARCHAR(20) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`type` VARCHAR(20) NOT NULL,
`filename` VARCHAR(151) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `DMR`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`type` VARCHAR(20) NOT NULL,
`filename` VARCHAR(151) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `SQP`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`unpassstage` VARCHAR(20) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `SQP_IQC`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`unpassstage` VARCHAR(20) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `SQP_PQC`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`unpassstage` VARCHAR(20) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `SQP_FQC`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`unpassstage` VARCHAR(20) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE `SQP_DOA`(
`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
`author` VARCHAR(100) NOT NULL,
`mark` VARCHAR(20) NOT NULL,
`unpassstage` VARCHAR(20) NOT NULL,
`code` VARCHAR(30) NOT NULL,
`date` DATE,
PRIMARY KEY(`id`)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;


### （4）初始化数据库，（因为用于不合品文件号生成 需要依赖4个分表中id最大的记录，因此需要另外在这4个表中插入一个初始数据）

#### 1.针对DHF、DMR设置主键初始值（300替换为各个类型文件号  需要的生成的第一个的数字尾号）
ALTER TABLE DHF AUTO_INCREMENT=300;
ALTER TABLE DMR AUTO_INCREMENT=300;

#### 2.针对SQP插入初始头记录 (lastcode 2023-05-01 分别替换为 该类型不合品文件号 最后一个文件号的 编号 与 日期)
INSERT INTO SQP_IQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_PQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_FQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_DOA(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");

#### 3.更改main.cpp中的数据库用户名及密码
#### 4.build
    sh ./build.sh
#### 5.启动server
    ./server

## 初始化操作
### （1）连接服务器
ssh ronovo@192.168.12.108  pass123
### （2）登入mysql
mysql -u root -p
Ronovo.1234567890
### （3）切换数据库
use filecodeserver


### （4）初始化数据库，（因为用于不合品文件号生成 需要依赖4个分表中id最大的记录，因此需要另外在这4个表中插入一个初始数据）
#### 1.清空表，主键初始化
truncate table DHF;
truncate table DMR;
truncate table SQP;
truncate table SQP_IQC;
truncate table SQP_PQC;
truncate table SQP_FQC;
truncate table SQP_DOA;

#### 2.针对DHF、DMR设置主键初始值（300替换为各个类型文件号  需要的生成的第一个的数字尾号）
ALTER TABLE DHF AUTO_INCREMENT=300;
ALTER TABLE DMR AUTO_INCREMENT=300;

#### 3.针对SQP插入初始头记录 (lastcode 2023-05-01 分别替换为 该类型不合品文件号 最后一个文件号的 编号 与 日期)
INSERT INTO SQP_IQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_PQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_FQC(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
INSERT INTO SQP_DOA(author, mark, unpassstage, code, date) VALUES("朱耀辉", "test", "test", "lastcode", "2023-05-01");
