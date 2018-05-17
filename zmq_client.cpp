
#include <map>
#include <string.h>
#include <zmq.h>

std::map<char, int> clientDB;

int main()
{
	//Prepare our context and socket
	void *ctx = zmq_ctx_new();
	void *subscriber = zmq_socket(ctx, ZMQ_SUB);
	zmq_connect(subscriber, "ipc:///tmp/zmq-publish.ipc");
	zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, 0, 0);
	void *collector = zmq_socket(ctx, ZMQ_PUSH);
	zmq_connect(collector, "ipc:///tmp/zmq-collect.ipc");
	void *snapshot = zmq_socket(ctx, ZMQ_DEALER);
	zmq_setsockopt(snapshot, ZMQ_IDENTITY, "C1", 2);
	zmq_connect(snapshot, "ipc:///tmp/zmq-snapshot.ipc");
	printf("Connecting to server...\n");

	char buffer[1024];
	long long sequence = 0;

	//ask for snapshot
	//zmq_send(snapshot, "C1", 2, ZMQ_SNDMORE);
	//zmq_send(snapshot, "", 0, ZMQ_SNDMORE);
	zmq_send(snapshot, "NEED_SNAPSHOT", 13, 0);

	int64_t more;
	size_t more_size = sizeof(more);

	char id[2];
	zmq_recv(snapshot, buffer, 1024, 0);
	memcpy(id, buffer, 2);
	printf("ID: %c%c\n", id[0], id[1]);
	zmq_recv(snapshot, buffer, 1024, 0);
	memcpy(&sequence, buffer, sizeof(long long));
	printf("SEQ: %lld\n", sequence);
	zmq_getsockopt(snapshot, ZMQ_RCVMORE, &more, &more_size);
	while (more & ZMQ_RCVMORE)
	{
		zmq_recv(snapshot, buffer, 1024, 0);
		int value = 0;
		memcpy(&value, buffer + 1, sizeof(int));
		//update client DB
		clientDB[buffer[0]] = value;
		printf("CL UPD: %c %d\n", buffer[0], value);
		zmq_getsockopt(snapshot, ZMQ_RCVMORE, &more, &more_size);
	}
	//get server updates
	//multiplex input
	zmq_pollitem_t items[] = {{subscriber, 0, ZMQ_POLLIN, 0}};

	int local_cnt = 0;
	while (true)
	{
		int rc = zmq_poll(items, 1, -1);

		//apply update
		if (items[0].revents & ZMQ_POLLIN)
		{
			int rsize = zmq_recv(subscriber, buffer, 1024, 0);
			if (rsize > 1024) continue;//--^
			char key = buffer[0];
			int value = 0;
			long long got_seq = 0;
			memcpy(&value, buffer + 1, sizeof(int));
			memcpy(&got_seq, buffer + 1 + sizeof(int), sizeof(long long));
			if (got_seq > sequence)
			{
				clientDB[key] = value;
				sequence = got_seq;
				printf("cl got mes: %lld %c %d\n", sequence, key, value);
			}
		}

		++local_cnt;
		if (local_cnt % 3 == 0)
		{
			sprintf(buffer, "C");
			int value = 1000 + local_cnt;
			memcpy(buffer + 1, &value, sizeof(int));
			buffer[1 + sizeof(int)] = '\0';
			printf("Client update: %c %d\n", buffer[0], value);
			zmq_send(collector, buffer, 1 + sizeof(int), 0);
		}

		//debug
		if (local_cnt % 10 == 0)
		{
			printf("CLIENT DB DUMP:\n");
			for (std::map<char, int>::iterator it = clientDB.begin(); it != clientDB.end(); ++it)
				printf("K: %c V: %d\n", it->first, it->second);
		}
	}

	zmq_close(snapshot);
	zmq_close(collector);
	zmq_close(subscriber);
	zmq_ctx_term(ctx);

	return 0;
}
