/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef GRPC_C_GRPC_C_H
#define GRPC_C_GRPC_C_H

#include <sys/queue.h>
#include <string.h>

#include <protobuf-c/protobuf-c.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HOSTPROG
#define PATH_GRPC_C_DAEMON_SOCK "unix:/tmp/grpc_c_"
#else
#define PATH_GRPC_C_DAEMON_SOCK "unix:" PATH_VAR_RUN_DIR "grpc_c_"
#endif

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

/*
 * Tracing levels and related functions
 */
#define GRPC_C_TRACE_TCP (1 << 0)
#define GRPC_C_TRACE_CHANNEL (1 << 1)
#define GRPC_C_TRACE_SURFACE (1 << 2)
#define GRPC_C_TRACE_HTTP (1 << 3)
#define GRPC_C_TRACE_FLOWCTL (1 << 4)
#define GRPC_C_TRACE_BATCH (1 << 5)
#define GRPC_C_TRACE_CONNECTIVITY_STATE (1 << 6)
#define GRPC_C_TRACE_SECURE_ENDPOINT (1 << 7)
#define GRPC_C_TRACE_TRANSPORT_SECURITY (1 << 8)
#define GRPC_C_TRACE_ROUND_ROBIN (1 << 9)
#define GRPC_C_TRACE_HTTP_WRITE_STATE (1 << 10)
#define GRPC_C_TRACE_API (1 << 11)
#define GRPC_C_TRACE_CHANNEL_STACK_BUILDER (1 << 12)
#define GRPC_C_TRACE_HTTP1 (1 << 13)
#define GRPC_C_TRACE_COMPRESSION (1 << 14)
#define GRPC_C_TRACE_QUEUE_PLUCK (1 << 15)
#define GRPC_C_TRACE_QUEUE_TIMEOUT (1 << 16)
#define GRPC_C_TRACE_OP_FAILURE (1 << 17)
#define GRPC_C_TRACE_CORE (1 << 18)
#define GRPC_C_TRACE_ALL (~0)

/*
 * Write return status. This should eventually become an enum and
 * writer->write should return that type instead of int
 */
#define GRPC_C_WRITE_OK 0
#define GRPC_C_WRITE_FAIL 1
#define GRPC_C_WRITE_PENDING 2

/*
 * GRPC-C return status codes
 */
#define GRPC_C_OK 0
#define GRPC_C_FAIL 1
#define GRPC_C_TIMEOUT 2

typedef void (grpc_c_trace_callback_t)(int priority, const char *file, 
				       int line, const char *message);

void grpc_c_set_trace_callback (grpc_c_trace_callback_t *fn);
void grpc_c_trace_enable (int flags, int severity);
void grpc_c_trace_disable (int flags);

/*
 * Forward declarations
 */
typedef struct grpc_c_server_s grpc_c_server_t;
typedef struct grpc_c_client_s grpc_c_client_t;
typedef struct grpc_c_context_s grpc_c_context_t;
typedef struct grpc_c_method_funcs_s grpc_c_method_funcs_t;
typedef struct grpc_c_read_handler_s grpc_c_read_handler_t;
typedef struct grpc_c_write_handler_s grpc_c_write_handler_t;
typedef struct grpc_c_stream_handler_s grpc_c_stream_handler_t;
typedef grpc_metadata_array grpc_c_metadata_array_t;

/*
 * Callbacks for client connect/disconnect at server
 */
typedef void (grpc_c_client_connect_callback_t)(const char *client_id);

typedef void (grpc_c_client_disconnect_callback_t)(const char *client_id);

/*
 * Callbacks for server connect and disconnect at client
 */
typedef void (grpc_c_server_connect_callback_t)(grpc_c_client_t *client);

typedef void (grpc_c_server_disconnect_callback_t)(grpc_c_client_t *client);

/*
 * Type of libgrpc used with libgrpc-c
 */
typedef enum gc_grpc_type_s {
    GRPC_THREADS,   /* Vanilla grpc */
    GRPC_ISC,	    /* libgrpc-isc based on libisc2 */
    GRPC_TASK,	    /* libgrpc-task based on libjtask */
    GRPC_NUM_TYPES,
} gc_grpc_type_t;

/*
 * Types of events that we use with tag when batching gRPC operations
 */
typedef enum grpc_c_event_type_s {
    GRPC_C_EVENT_RPC_INIT,
    GRPC_C_EVENT_CLEANUP, 
    GRPC_C_EVENT_METADATA,
    GRPC_C_EVENT_READ,
    GRPC_C_EVENT_WRITE,
    GRPC_C_EVENT_READ_FINISH,
    GRPC_C_EVENT_WRITE_FINISH,
    GRPC_C_EVENT_RECV_CLOSE,
    GRPC_C_EVENT_SERVER_SHUTDOWN,
} grpc_c_event_type_t;

/*
 * Event structure to be used as tag when batching gRPC operations
 */
typedef struct grpc_c_event_s {
    grpc_c_event_type_t gce_type;   /* Type of this event */
    void *gce_data;		    /* Data associated with this event */
    int gce_refcount;		    /* Refcount on this event */
} grpc_c_event_t;

/*
 * Structure containing pointers to functions at grpc layer
 */
typedef struct grpc_c_hook_s {
    gc_grpc_type_t gch_type;	/* Type of underlying libgrpc */
    void (*gch_set_cq_callback)(grpc_completion_queue *cq, 
				int (*callback)(grpc_completion_queue *cq));
				/* Function to set callback on completion 
				 * queue if underlying grpc layer supports it */
    const char *(*gch_get_client_id)(grpc_call *call);
				/* Get client id from call */
    void (*gch_set_client_id)(grpc_call *call, const char *id);
				/* Function to set client id to transport */
    void (*gch_set_disconnect_cb)(grpc_call *call, 
				  grpc_c_client_disconnect_callback_t *cb);
				/* Sets client disconnect callback in 
				 * transport */
    void (*gch_set_evcontext)(void *evcontext);	/* Sets evcontext if isc */
    void (*gch_set_client_task)(void *tp);	/* Sets client task if jtask */
    void (*gch_set_client_socket_create_cb)(void (*fp)(int fd, const char *uri));
				/* Sets callback to be called when client
				 * socket is created */
    int (*gch_client_try_connect)(long timeout, 
				  void (*timeout_cb)(void *data), 
				  void *timeout_cb_arg, void **tag);
				/* Function that client can call to try
				 * establishing a connection */
    void (*gch_client_cancel_try_connect)(void *closure);
				/* Function to cancel connection retry
				 * attempts on client side */
    void (*gch_post_init)(void);/* Post grpc init function */
} grpc_c_hook_t;

/*
 * Initialize libgrpc-c to be used with given underlying libgrpc. Second
 * parameter is used to pass data to underlying library if it needs any
 */
void grpc_c_init (gc_grpc_type_t type, void *data);

/*
 * Shutsdown initialized grpc-c library. To be called towards end of program
 */
void grpc_c_shutdown (void);

/*
 * Initializes grpc hook data
 */
void grpc_c_hook_init (grpc_c_hook_t *hook);

/*
 * Definition for RPC method structure
 */
struct grpc_c_method_t {
    void *gcm_tag;			/* Tag returned by 
					   grpc_server_register_method() */
    char *gcm_name;			/* URL for this RPC */
    int gcm_method_id;			/* Index of this method in the list of 
					   all registered methods */
    int gcm_client_streaming;		/* Flag to indicate if client is 
					   streaming */
    int gcm_server_streaming;		/* Flag to indicate if server is 
					   streaming */
    LIST_ENTRY(grpc_c_method_t) gcm_list;
					/* List of all the registered methods */
};

/*
 * List of all the possible states for grpc_c client and server
 */
typedef enum grpc_c_state_s {
    GRPC_C_OP_START,		/* Started executing operations */
    GRPC_C_SERVER_CALLBACK_WAIT,    /* Waiting to call RPC handler in server */
    GRPC_C_SERVER_CALLBACK_START,   /* Called RPC handler */
    GRPC_C_SERVER_CALLBACK_DONE,    /* RPC handler finished execution and 
				       returned */
    GRPC_C_SERVER_CONTEXT_CLEANUP,  /* Context is cleaned up */
    GRPC_C_SERVER_CONTEXT_NOOP,	/* Context is cleaned up */
    GRPC_C_READ_DATA_START,	/* Started reading incoming data */
    GRPC_C_READ_DATA_DONE,	/* Finished reading incoming data */
    GRPC_C_WRITE_DATA_START,	/* Started writing data */
    GRPC_C_WRITE_DATA_DONE,	/* Finished writing data */
    GRPC_C_CLIENT_START,	/* Started RPC from client */
    GRPC_C_CLIENT_DONE,		/* Finished RPC from client */
} grpc_c_state_t;


/*
 * Client structure related definitions
 */

/*
 * Structure definition for grpc_c client
 */
struct grpc_c_client_s {
    grpc_channel *gcc_channel;	    /* Underlying grpc channel to host */
    grpc_completion_queue *gcc_cq;  /* Completion queue associated with this 
				       client */
    grpc_completion_queue *gcc_channel_connectivity_cq;
				    /* Completion queue to receive channel
				       connectivity change events */
    gpr_slice gcc_host;		    /* Hostname of remote providing RPC 
				       service */
    char *gcc_id;		    /* Client identification string */
    int gcc_channel_state;	    /* Channel connectivity state */
    int gcc_connected;		    /* Connection status */
    int gcc_conn_timeout;	    /* Connection timeout flag */
    void *gcc_retry_tag;	    /* Retry tag to be used when stopping 
				       reconnection attempts */
    grpc_c_server_connect_callback_t *gcc_server_connect_cb;
				    /* Callback to call when server connection
				       is established */
    grpc_c_server_disconnect_callback_t *gcc_server_disconnect_cb;
				    /* Callback when server connection is
				       dropped */
    gpr_mu gcc_lock;		    /* Mutex lock */
    gpr_cv gcc_callback_cv;	    /* Callback condition variable */
    gpr_cv gcc_shutdown_cv;	    /* Shutdown condition variable */
    int gcc_running_cb;		    /* Current running callbacks */
    int gcc_shutdown;		    /* Client shutdown flag */
    int gcc_wait;		    /* Waiting flag */
    LIST_HEAD(grpc_c_context_list_head, grpc_c_context_s) gcc_context_list_head;
				    /* List of active context objects */
};

/*
 * Signature for client callback
 */
typedef void (grpc_c_client_callback_t)(grpc_c_context_t *context, void *data, 
					int success);

/*
 * Signature for read pending callback
 */
typedef void (grpc_c_read_resolve_callback_t)(grpc_c_context_t *context, 
					      void *data, int success);

/*
 * Signature for write pending callback
 */
typedef void (grpc_c_write_resolve_callback_t)(grpc_c_context_t *context, 
					       void *data, int success);

/*
 * Initialize a client with client_id to server_name. We build unix domain
 * socket path to server from server_name. When channel_creds is given, we
 * create a secure channel. Otherwise it'll be an insecure one.
 */
grpc_c_client_t *
grpc_c_client_init (const char *server_name, const char *client_id, 
		    grpc_channel_credentials *channel_creds, 
		    grpc_channel_args *channel_args); 

/*
 * Initialize a client with client_id and server address
 */
grpc_c_client_t *
grpc_c_client_init_by_host (const char *address, const char *client_id, 
			    grpc_channel_credentials *channel_creds, 
			    grpc_channel_args *channel_args); 

/*
 * Waits for all callbacks to get done in a threaded client
 */
void grpc_c_client_wait (grpc_c_client_t *client);

/*
 * Destroy and free client object
 */
void grpc_c_client_free (grpc_c_client_t *client);

/*
 * Register a callback that gets called when server connection is established
 */
void 
grpc_c_register_server_connect_callback (grpc_c_client_t *client, 
					 grpc_c_server_connect_callback_t *cb);

/*
 * Register a callback that gets called when server connection is dropped
 */
void 
grpc_c_register_server_disconnect_callback (grpc_c_client_t *client, 
				    grpc_c_server_disconnect_callback_t *cb);


/*
 * Structure definition for grpc-c context
 */
struct grpc_c_context_s {
    struct grpc_c_method_t *gcc_method;		/* Corresponding method */
    grpc_byte_buffer *gcc_payload;		/* Payload holder */
    grpc_op *gcc_ops;				/* Array of grpc operations */
    grpc_byte_buffer **gcc_ops_payload;		/* Payload per operation */
    grpc_completion_queue *gcc_cq;		/* Completion queue associated 
						   with this context */
    gpr_timespec gcc_deadline;			/* Deadline for operations in 
						   this context */
    grpc_c_metadata_array_t *gcc_metadata;	/* Metadata array to send 
						   metadata with each call */
    char **gcc_metadata_storage;		/* Array pointing to key value 
						   pair storage */
    grpc_c_metadata_array_t *gcc_initial_metadata;  /* Initial metadata array */
    char **gcc_initial_metadata_storage;	/* Array containing intitial 
						   metadata key value pairs */
    grpc_c_metadata_array_t *gcc_trailing_metadata; /* Trailing metadata array */
    char **gcc_trailing_metadata_storage;	/* Array containing trailing 
						   metadata key value pairs */
    void *gcc_tag;				/* User provided tag to 
						   identify context in 
						   callbacks */
    int gcc_meta_sent;				/* Flag to mark that initial 
						   metadata is sent */
    int gcc_is_client;				/* Flag to mark if context 
						   belongs to client */
    int gcc_op_count;				/* Number of pending grpc 
						   operations */
    int gcc_op_capacity;			/* Capacity of gcc_ops array */
    grpc_c_state_t gcc_state;			/* Current state of 
						   client/server */
    grpc_call *gcc_call;			/* grpc_call for this RPC */
    gpr_mu *gcc_lock;				/* Mutex for access to this cq */
    grpc_status_code gcc_status;		/* Result of RPC execution */
    grpc_slice gcc_status_details;		/* Status details from RPC 
						   execution */
    grpc_c_method_funcs_t *gcc_method_funcs;	/* Pointer to method functions 
						   like input/output packer, 
						   unpacker, free and method 
						   callbacks */
    grpc_c_write_handler_t *gcc_writer;		/* Write handler that provides 
						   write and finish methods to 
						   user callback */
    grpc_c_read_handler_t *gcc_reader;		/* Read handler that provides 
						   read, finish and free 
						   methods to user callback */
    grpc_c_stream_handler_t *gcc_stream;	/* Handle to IO stream. Users 
						   can use this handle to 
						   read/write into the call */
    grpc_c_read_resolve_callback_t *gcc_read_resolve_cb;
						/* Read callback that can be
						   called when pending read is
						   finished */
    void *gcc_read_resolve_arg;			/* Data that can be passed to 
						   when calling read resolve 
						   cb */
    grpc_c_write_resolve_callback_t *gcc_write_resolve_cb;
						/* Write callback that can be 
						   called when a pending write 
						   is finished */
    void *gcc_write_resolve_arg;		/* Identifying data that can 
						   be passed to user provided 
						   callback when a write is 
						   finished */
    int gcc_call_cancelled;			/* Boolean indicating that 
						   call has been cancelled */
    int gcc_client_cancel;			/* Boolean indicating if 
						   client has cancelled the 
						   call */
    grpc_c_event_t gcc_event;			/* grpc-c event this context 
						   belongs to */
    grpc_c_event_t gcc_read_event;		/* Event tag for read ops */
    grpc_c_event_t gcc_write_event;		/* Event tag for write ops */
    grpc_c_event_t gcc_write_done_event;	/* Event tag for write done 
						   from client */
    grpc_c_event_t gcc_recv_close_event;	/* Recv close grpc-c event in 
						   case of server context */
    union {					/* Union containing client or 
						   server object */
	grpc_c_server_t *gccd_server;
	grpc_c_client_t *gccd_client;
    } gcc_data;
    LIST_ENTRY(grpc_c_context_s) gcc_list;	/* List of context objects */
};

/*
 * Structure definition for return status of RPC
 */
typedef struct grpc_c_status_s {
    int gcs_code;
    char gcs_message[4 * BUFSIZ];
} grpc_c_status_t;

/*
 * Function to set write resolve callback and args
 */
static inline void 
grpc_c_set_write_done (grpc_c_context_t *context, 
		       grpc_c_write_resolve_callback_t *cb, void *data) 
{
    if (context != NULL) {
	context->gcc_write_resolve_cb = cb;
	context->gcc_write_resolve_arg = data;
    }
}

static inline void 
grpc_c_set_read_done (grpc_c_context_t *context, 
		      grpc_c_read_resolve_callback_t *cb, void *data)
{
    if (context != NULL) {
	context->gcc_read_resolve_cb = cb;
	context->gcc_read_resolve_arg = data;
    }
}

/*
 * Function to check if we have a pending write
 */
static inline int 
grpc_c_is_write_pending (grpc_c_context_t *context) 
{
    if (context != NULL && context->gcc_state == GRPC_C_WRITE_DATA_START) {
	return 1;
    }

    return 0;
}

/*
 * Stream read, write and finish function signatures 
 */
typedef int (grpc_c_read_t)(grpc_c_context_t *context, void **content, 
			    uint32_t flags, long timeout);

typedef int (grpc_c_write_t)(grpc_c_context_t *context, void *output, 
			     uint32_t flags, long timeout);

typedef int (grpc_c_read_finish_t)(grpc_c_context_t *context, 
				   grpc_c_status_t *status, uint32_t flags);

typedef int (grpc_c_write_finish_t)(grpc_c_context_t *context, int status, 
				    const char *message, uint32_t flags);

typedef int (grpc_c_stream_read_t)(grpc_c_context_t *context, void **content, 
				   uint32_t flags, long timeout);

typedef int (grpc_c_stream_write_t)(grpc_c_context_t *context, void *output, 
				    uint32_t flags, long timeout);

typedef int (grpc_c_stream_write_done_t)(grpc_c_context_t *context, 
					 uint32_t flags, long timeout);

typedef int (grpc_c_stream_finish_t)(grpc_c_context_t *context, 
				     grpc_c_status_t *status, uint32_t flags);

typedef void (grpc_c_method_data_free_t)(grpc_c_context_t *context, void *buf);

/*
 * Read handler
 */
struct grpc_c_read_handler_s {
    grpc_c_read_t *read;
    grpc_c_read_finish_t *finish;
    grpc_c_method_data_free_t *free;
};

/*
 * Write handler
 */
struct grpc_c_write_handler_s {
    grpc_c_write_t *write;
    grpc_c_write_finish_t *finish; 
    grpc_c_method_data_free_t *free;
};

/*
 * Stream handler
 */
struct grpc_c_stream_handler_s {
    grpc_c_stream_read_t *read;
    grpc_c_stream_write_t *write;
    grpc_c_stream_write_done_t *write_done;
    grpc_c_stream_finish_t *finish;
};

/*
 * Service implementation
 */
typedef void (grpc_c_service_callback_t)(grpc_c_context_t *context);


/*
 * Data packer to wire format
 */
typedef size_t (grpc_c_method_data_pack_t)(void *input, 
					   grpc_byte_buffer **buffer);

/*
 * Data unpacker from wire format
 */
typedef void *(grpc_c_method_data_unpack_t)(grpc_c_context_t *context, 
					    grpc_byte_buffer *input);

/*
 * Structure definition for method functions
 */
struct grpc_c_method_funcs_s {
    char *gcmf_name;					/* URL for this RPC */
    union {
	grpc_c_service_callback_t *gcmfh_server;	/* RPC handler */
	grpc_c_client_callback_t *gcmfh_client;		/* Client callback */
    } gcmf_handler;
    grpc_c_method_data_pack_t *gcmf_input_packer;	/* Input packer */
    grpc_c_method_data_unpack_t *gcmf_input_unpacker;	/* Input unpacker */
    grpc_c_method_data_free_t *gcmf_input_free;		/* Input free function */
    grpc_c_method_data_pack_t *gcmf_output_packer;	/* Output packer */
    grpc_c_method_data_unpack_t *gcmf_output_unpacker;	/* Output unpacker */
    grpc_c_method_data_free_t *gcmf_output_free;	/* Output free function */
};

/*
 * User provided memory alloc and free functions
 */
typedef void *(grpc_c_memory_alloc_func_t)(grpc_c_context_t *context, 
					   size_t size);

typedef void (grpc_c_memory_free_func_t)(grpc_c_context_t *context, void *p);

void grpc_c_set_memory_alloc_function (grpc_c_memory_alloc_func_t *fn);

void grpc_c_set_memory_free_function (grpc_c_memory_free_func_t *fn);

ProtobufCAllocator *
grpc_c_get_protobuf_c_allocator (grpc_c_context_t *context, 
				 ProtobufCAllocator *allocator);


/*
 * Server structure definition
 */
struct grpc_c_server_s {
    char *gcs_host;			    /* Server hostname */
    grpc_server *gcs_server;		    /* Grpc server */
    grpc_completion_queue *gcs_cq;	    /* Server completion queue */
    grpc_c_method_funcs_t *gcs_method_funcs;/* Array of methods */
    LIST_HEAD(grpc_c_method_list_head, grpc_c_method_t) gcs_method_list_head;
    int gcs_method_count;		    /* Number of registered methods */
    grpc_c_client_connect_callback_t *gcs_client_connect_cb;	/* Callback to 
					    call when a new client connects */
    grpc_c_client_disconnect_callback_t *gcs_client_disconnect_cb; /* Callback 
						    when client disconnects */
    grpc_c_context_t **gcs_contexts;	    /* List of context objects waiting 
					       on methods */
    int gcs_running_cb;			    /* Number of currently running 
					       callbacks */
    gpr_mu gcs_lock;			    /* Mutex lock */
    gpr_cv *gcs_callback_cv;		    /* Callback condition variable */
    gpr_cv gcs_shutdown_cv;		    /* Shutdown condition variable */
    gpr_cv gcs_cq_destroy_cv;		    /* Completion queue destroy cv */
    int gcs_shutdown;			    /* Server shutting down */
    int *gcs_callback_shutdown;		    /* Shadow value so 
					       grpc_c_server_wait() can 
					       consume */
    int *gcs_callback_running_cb;	    /* Shadow running callback count */
    int gcs_cq_shutdown;		    /* Boolean to indicate that server 
					       completion queue has shutdown */
    grpc_c_event_t gcs_shutdown_event;	    /* Event signalling server shutdown */
};

/*
 * Start server
 */
int grpc_c_server_start (grpc_c_server_t *server);

/*
 * Makes a threaded server block
 */
void grpc_c_server_wait (grpc_c_server_t *server);

/*
 * Create a server object with given daemon name. We build unix domain socket
 * path from this name
 */
grpc_c_server_t *
grpc_c_server_create (const char *name, 
		      grpc_server_credentials *creds, 
		      grpc_channel_args *args);

/*
 * Create a server object with given address
 */
grpc_c_server_t *
grpc_c_server_create_by_host (const char *addr, grpc_server_credentials *creds, 
			      grpc_channel_args *args);

/*
 * Destroy and free grpc-c server
 */
void grpc_c_server_destroy (grpc_c_server_t *server);

/*
 * Allocate memory for methods on this server
 */
int grpc_c_methods_alloc (grpc_c_server_t *server, int method_count);

/*
 * Register a list of methods
 */
void grpc_c_register_methods (const char *methods[], int method_count);

/*
 * Register a method along with corresponding method functions
 */
int grpc_c_register_method (grpc_c_server_t *server, const char *method, 
			    int client_streaming, int server_streaming, 
			    grpc_c_service_callback_t *handler, 
			    grpc_c_method_data_pack_t *input_packer, 
			    grpc_c_method_data_unpack_t *input_unpacker, 
			    grpc_c_method_data_free_t *input_free, 
			    grpc_c_method_data_pack_t *output_packer, 
			    grpc_c_method_data_unpack_t *output_unpacker, 
			    grpc_c_method_data_free_t *output_free);

/*
 * Add insecure ip/port to grpc server. Returns 0 on failure
 */
int grpc_c_server_add_insecure_http2_port (grpc_c_server_t *server, 
					   const char *addr);

/*
 * Add secure ip/port to grpc server. Returns 0 on failure
 */
int grpc_c_server_add_secure_http2_port (grpc_c_server_t *server, 
					 const char *addr, 
					 grpc_server_credentials *creds);

/*
 * Checks if the channel is valid on this context. Returns 1 if cancelled and
 * 0 otherwise
 */
int grpc_c_context_is_call_cancelled (grpc_c_context_t *context);

/*
 * Register connect callback on server
 */
void 
grpc_c_register_connect_callback (grpc_c_server_t *server, 
				  grpc_c_client_connect_callback_t *cb);

/*
 * Registers disconnect callback on server
 */
void 
grpc_c_register_disconnect_callback (grpc_c_server_t *server, 
				     grpc_c_client_disconnect_callback_t *cb);

/*
 * Metadata array
 */
void grpc_c_metadata_array_init (grpc_c_metadata_array_t *array);

void grpc_c_metadata_array_destroy (grpc_c_metadata_array_t *array);

/*
 * Extract the value from metadata by key. Return NULL if not found
 */
const char *
grpc_c_get_metadata_by_key (grpc_c_context_t *context, const char *key);

/*
 * Extract the value from initial metadata by key. Return NULL if not found
 */
const char *
grpc_c_get_initial_metadata_by_key (grpc_c_context_t *context, 
				    const char *key);

/*
 * Extract the value from trailing metadata by key. Return NULL if not found
 */
const char *
grpc_c_get_trailing_metadata_by_key (grpc_c_context_t *context, 
				     const char *key);

/*
 * Adds given key value pair to metadata array of given context. This will be
 * used by clients when sending metadata to server. Returns 0 on success and 1
 * on failure
 */
int 
grpc_c_add_metadata (grpc_c_context_t *context, const char *key, 
		     const char *value);

/*
 * Adds given key value pair to initial metadata array of given context.
 * Returns 0 on success and 1 on failure
 */
int 
grpc_c_add_initial_metadata (grpc_c_context_t *context, const char *key, 
			     const char *value);

/*
 * Adds given key value pair to trailing metadata array of given context.
 * Returns 0 on success and 1 on failure
 */
int 
grpc_c_add_trailing_metadata (grpc_c_context_t *context, const char *key, 
			      const char *value);

/*
 * sends immediately the available initial metadata from server.
 * NOTE: This will block the caller till the initial metadata is sent to the
 * receiver. If this is not called, all the added initial metadata will be
 * sent upon first write from server
 */
int grpc_c_send_initial_metadata (grpc_c_context_t *context, long timeout);

/*
 * Get client-id from context
 */
const char *grpc_c_get_client_id (grpc_c_context_t *context);

int grpc_c_client_request_unary (grpc_c_client_t *client, 
				 grpc_c_metadata_array_t *array, uint32_t flags,  
				 const char *method, 
				 void *input, void **output, 
				 grpc_c_status_t *status, 
				 int client_streaming, int server_streaming, 
				 grpc_c_method_data_pack_t *input_packer, 
				 grpc_c_method_data_unpack_t *input_unpacker, 
				 grpc_c_method_data_free_t *input_free, 
				 grpc_c_method_data_pack_t *output_packer, 
				 grpc_c_method_data_unpack_t *output_unpacker, 
				 grpc_c_method_data_free_t *output_free, 
				 long timeout);
/*
 * Main function for non-streaming/synchronous RPC call from client
 */
int grpc_c_client_request_sync (grpc_c_client_t *client, 
				grpc_c_metadata_array_t *mdarray, uint32_t flags, 
				grpc_c_context_t **context, const char *method, 
				void *inpupt, int client_streaming, 
				int server_streaming, 
				grpc_c_method_data_pack_t *input_packer, 
				grpc_c_method_data_unpack_t *input_unpacker, 
				grpc_c_method_data_free_t *input_free, 
				grpc_c_method_data_pack_t *output_packer, 
				grpc_c_method_data_unpack_t *output_unpacker, 
				grpc_c_method_data_free_t *output_free, 
				long timeout);

/*
 * Main function for asynchronous/streaming RPC call from client
 */
int grpc_c_client_request_async (grpc_c_client_t *client, 
				 grpc_c_metadata_array_t *mdarray, uint32_t flags, 
				 const char *method, 
				 void *input, grpc_c_client_callback_t *cb, 
				 void *tag, int client_streaming, 
				 int server_streaming, 
				 grpc_c_method_data_pack_t *input_packer, 
				 grpc_c_method_data_unpack_t *input_unpacker, 
				 grpc_c_method_data_free_t *input_free, 
				 grpc_c_method_data_pack_t *output_packer, 
				 grpc_c_method_data_unpack_t *output_unpacker, 
				 grpc_c_method_data_free_t *output_free);

/*
 * Initialize task
 */
void grpc_c_task_init (void *tp);

/*
 * Set jtask
 */
void grpc_c_set_task (void *tp);

/*
 * Set client task
 */
void grpc_c_set_client_task (void *tp);

/*
 * Set evContext for libisc
 */
void grpc_c_set_evcontext (void *evContext);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_C_GRPC_C_H */
