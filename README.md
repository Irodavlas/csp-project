# csp-project

## 1. How to compile the code 
### Server
    make server
### client
    make client
## 2. Start client and servers
### Server 
    $ sudo bin/server <HomeDir> [ip] [port]
### Client
    $ bin/client [ip] [port]
## 3. How to execute commands and expected outputs

### create_user <username> <permissions (octal)>
    Input: create_user user 0740
    Expected output: User created succesfully

### login <username>
    Input: login user
    Expected output: Login successful

### create <path> <permissions (in octal)> [-d] 
    Input: create file.txt 0740 | create dir 0740 -d
    Expected output: File created succesfully | Directory created successfully

### chmod <path> <permissions (in octal)>
    Input: chmod file.txt 0700
    Expected output: Permissions updated successfully

### move <path1> <path2>
    Input: move file.txt dir/
    Expected output: Moved successfully

### upload <client_path> <server_path> [-b] 
    Input: upload file.txt copy.txt | upload file.txt copy.txt -b
    Expected output: upload copy.txt file.txt concluded

### download <server_path> <client_path> [-b]
    Input: download copy.txt copy1.txt | download copy.txt copy1.txt -b
    Expected output: download copy.txt copy1.txt concluded

### cd <path> 
    Input: cd dir
    Expected output: Current workDir: /dir

### ls <path>
    Input: ls .
    Expected output: -rwx------  file.txt                      0 bytes

### read [-offset=N] <path>
    Input: read -offset=0 file.txt | read file.txt

### write [-offset=N] <path>
    write -offset=0 copy.txt | write copy.txt

### delete <path>
    Input: delete copy.txt | delete dir
    Expected output: Deleted successfully

### transfer_request <file> <dest_user>
    Input: transfer_request file.txt test
    Expected output: 
    Reject: [NOTIFICATION] User test REJECTED your transfer: file.txt