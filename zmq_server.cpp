
#include <map>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

std::map<char, int> serverDB;

void *server_inner_update_routine(void *par)
{
	void *ctx = zmq_ctx_new();
	void *collector = zmq_socket(ctx, ZMQ_PUSH);
	zmq_connect(collector, "ipc:///tmp/zmq-collect.ipc");
	char buffer[1024];
	while (true)
	{
		sleep(3);
		static int value = 0;
		++value;
		sprintf(buffer, "I");
		memcpy(buffer + 1, &value, sizeof(int));
		buffer[1 + sizeof(int)] = '\0';
		printf("Inner server update: %c %d\n", buffer[0], value);
		zmq_send(collector, buffer, 1 + sizeof(int), 0);
	}
	zmq_close(collector);
	zmq_ctx_term(ctx);
	return 0;
}

int main()
{
	//initial server DB forming
	serverDB['1'] = 1;
	serverDB['2'] = 2;
	serverDB['3'] = 3;
	serverDB['4'] = 4;

	//sockets
	void *ctx = zmq_ctx_new();
	void *publisher = zmq_socket(ctx, ZMQ_PUB);
	zmq_bind(publisher, "ipc:///tmp/zmq-publish.ipc");
	void *collector = zmq_socket(ctx, ZMQ_PULL);
	zmq_bind(collector, "ipc:///tmp/zmq-collect.ipc");
	void *snapshot = zmq_socket(ctx, ZMQ_ROUTER);
	zmq_bind(snapshot, "ipc:///tmp/zmq-snapshot.ipc");

	//multiplex input
	zmq_pollitem_t items[] = {{collector, 0, ZMQ_POLLIN, 0}, {snapshot, 0, ZMQ_POLLIN, 0}};

	pthread_t inner_update_thread;
	pthread_create(&inner_update_thread, 0, server_inner_update_routine, 0);

	//main server loop
	long long sequence = 0;
	char buffer[1024];
	while (true)
	{
		//sleep until got some action on one of our sockets
		int rc = zmq_poll(items, 2, -1);

		//apply update
		if (items[0].revents & ZMQ_POLLIN)
		{
			int rsize = zmq_recv(collector, buffer, 1024, 0);
			if (rsize > 1024) continue;//--^
			char key = buffer[0];
			int value = 0;
			memcpy(&value, buffer + 1, sizeof(int));
			serverDB[key] = value;
			printf("srv rcvd upd: %c %d\n", key, value);
			
			//add sequence number
			memcpy(buffer + 1 + sizeof(int), &(++sequence), sizeof(long long));
			//publish global DB update
			zmq_send(publisher, buffer, 1 + sizeof(int) + sizeof(long long), 0);
		}

		//send snapshot on request
		if (items[1].revents & ZMQ_POLLIN)
		{
			int64_t more;
			size_t more_size = sizeof(more);

			//identity of socket wanting snapshot
			int rc = zmq_recv(snapshot, buffer, 1024, 0);
			if (rc != 2) continue;//--^

			char id[2];
			memcpy(id, buffer, 2);
			printf("ID: %c%c needs snapshot\n", id[0], id[1]);

			zmq_getsockopt(snapshot, ZMQ_RCVMORE, &more, &more_size);
			if (!(more & ZMQ_RCVMORE)) continue;//--^
			zmq_recv(snapshot, buffer, 1024, 0);
			if (strncmp(buffer, "NEED_SNAPSHOT", 13) == 0)
			{
				printf("Need to send snapshot to id %c%c\n", id[0], id[1]);
				//send identity
				memcpy(buffer, id, 2);
				zmq_send(snapshot, buffer, 2, ZMQ_SNDMORE);
				
				//send delimeter
				zmq_send(snapshot, "", 0, ZMQ_SNDMORE);
				
				//send sequence number
				memcpy(buffer, &sequence, sizeof(long long));
				zmq_send(snapshot, buffer, sizeof(long long), ZMQ_SNDMORE);
				
				// send snapshot
				for(std::map<char, int>::iterator it = serverDB.begin(); it != serverDB.end();)
				{
					char key = it->first;
					int value = it->second;
					printf("Snapshot K: %c V: %d\n", key, value);
					memcpy(buffer, &key, 1);
					memcpy(buffer + 1, &value, sizeof(int));
					++it;
					zmq_send(snapshot, buffer, sizeof(int) + 1, it == serverDB.end() ? 0 : ZMQ_SNDMORE);
				}
			}
		}
	}
	pthread_join(inner_update_thread, 0);

	zmq_close(snapshot);
	zmq_close(collector);
	zmq_close(publisher);
	zmq_ctx_term(ctx);
	return 0;
}

