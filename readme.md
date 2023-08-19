*  install mysql 8.0.33-0ubuntu0.20.04.2 
    ```
    sudo apt install mysql-sever
    sudo apt install libmysqlclient-dev

    #安装完成后运行之后的指令查询user和password
    sudo cat /etc/mysql/debian.cnf
    ![Alt text](image.png)

    #登录mysql
    mysql -u username(example : debian-sys-maint) -p password(example :  Ca1UpLIwbJqirHjP)

    #完成后执行下面指令修改root密码
    ALTER USER 'root'@'localhost' IDENTIFIED BY '123456';

    #执行使其work
    flush privileges;
    ```