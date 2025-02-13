-- 删除T_ZHOBTMIND表2小时之前的数据。
delete from T_ZHOBTMIND where ddatetime<sysdate-2/24;
commit;
exit;

