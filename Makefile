
server_files = \
	src/server/main.c \
	src/server/handler/handlers.c \
	src/server/helper/helper.c \
	src/server/utils/utils.c \
	src/server/net/net.c \
	src/server/core/server.c \
	src/common/utility.c

client_files = \
    src/client/main.c \
    src/client/worker.c \
    src/client/net.c \
    src/common/utility.c

server:
	gcc -I./src -I./src/server -I./src/common $(server_files) -o bin/server -lpthread

client:
	gcc -I./src -I./src/client -I./src $(client_files) -o bin/client -lpthread

clean:
	rm -f bin/server bin/client