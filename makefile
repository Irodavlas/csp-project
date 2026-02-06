# needs to create the unpriviledged user for server to drop to


server_files = \
	src/server/main.c \
	src/server/handler/handlers.c \
	src/server/helper/helper.c \
	src/server/utils/utils.c \
	src/server/net/net.c \
	src/server/server/server.c \
	src/common/utility.c

client_files = \
	src/client/main.c \
	src/client/net.c \
	src/common/utility.c


server:
	gcc -I./src -I./src/server -I./src/common $(server_files) -o server

client:
	gcc -I./src/client -I./src $(client_files) -o client 
