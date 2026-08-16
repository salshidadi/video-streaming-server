#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h> 

void handle_exit(int sig) {
    printf("\nShutting down client...\n");
    system("killall ffplay 2>/dev/null");
    exit(0);
}

int main()
{
    signal(SIGINT, handle_exit);
    
    system("ffplay -nodisp -f s16le -ar 44100 -ac 1 -i udp://127.0.0.1:1235 > /dev/null 2>&1 &");

    FILE *video_player = popen("ffplay -fflags nobuffer -flags low_delay -f h264 -i pipe:0 > /dev/null 2>&1", "w");

    int client_fd;
    struct sockaddr_in server_addr;

    client_fd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        return 1;
    }

    write(client_fd, "hello", 5);

    while (1)
    {
        char recv_buffer[65536];
        int bytes = read(client_fd, recv_buffer, sizeof(recv_buffer));
        if (bytes > 0)
        {
            fwrite(recv_buffer, 1, bytes, video_player);
            fflush(video_player); 
        }
    }

    pclose(video_player);
    close(client_fd);
    return 0;
}