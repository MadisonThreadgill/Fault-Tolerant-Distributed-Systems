import socket
import threading
import queue
import time
import Matrix_Creator as mc
import numpy as np
import select

HOST = socket.gethostname()
PORT = 6000

# The task queue holds the sub-tasks that need to be completed
task_queue = queue.Queue()

# The done queue holds the completed sub-tasks
done_queue = queue.Queue()

# sequential matrix multiplication
compare_queue = queue.Queue()

# Each task is a matrix multiplication
num_tasks = 5

for i in range(num_tasks):
    # Generate random sizes for the matrices
    rows1, cols1, rows2, cols2 = mc.generate_sizes()
    # Create matrices and fill with random numbers
    matrix1, matrix2 = mc.generate_matrices(rows1, cols1, rows2, cols2)
    # print matrix sizes for debug
    print("Matrix 1 size: ", rows1, cols1)
    print("Matrix 2 size: ", rows2, cols2)
    # print matrices for debug
    print("Matrix 1:")
    print(matrix1)
    print("Matrix 2:")
    print(matrix2)
    # print matrices multiplied for debug
    print("Matrix 1 * Matrix 2:")
    print(np.array(mc.multiply_matrices(matrix1, matrix2)))
    # Create the data to send
    data_to_send = mc.create_data_to_send(rows1, cols1, rows2, cols2, matrix1, matrix2)
    # Add the task to the queue
    task_queue.put(data_to_send)
    # Add the result to the compare queue
    compare_queue.put(np.array(mc.multiply_matrices(matrix1, matrix2)))


not_nums = ['[', ']', ',']
def compare_results():
    #for i in range(num_tasks):
    print("Comparing results...")
    for i in range(num_tasks):
        first = done_queue.get()   # from clients
        first = first.split()
        second = compare_queue.get()
        first_counter = 0
        # iterate through the rows of the matrix
        #for i in range(len(second)):
            # iterate through the columns of the matrix
            #for j in range(len(second[0])):
                # print the element at the current row and column
                #if str(first[first_counter]) != str(second[i][j]):
                    #print(first[first_counter], second[i][j])
        print(first)
        print(second)
        #return False
        first_counter += 1
    return True    

print(f"Task queue size is: {task_queue.qsize()}")

# Create a socket to listen for incoming connections from clients
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_address = (HOST, PORT)
print(f'Starting up on {server_address[0]} port {server_address[1]}' )
server_socket.bind(server_address)
ipAddr = server_socket.getsockname()
print(f'IP Address: {ipAddr}')
server_socket.listen(2)

current_task = []
lock = threading.Lock()
activateFlag = False

# The client_handler function is executed in a separate thread for each client
def client_handler_worker(client_socket):
    global current_task
    global activateFlag
    no_tasks = False
    client_ip, client_port = client_socket.getsockname()
    for s in client_sockets:
        print(s)
        
    while not no_tasks:
        # Receive messages from the client
        ready, _, _ = select.select([client_socket], [], [], 5)
        if ready:
            task = []
            message = client_socket.recv(20).decode("utf-8")
            print(f"Message from client {client_ip}: {message}")
        else:
            print("Client failed to send message, calling for backup")# Activate other device
            other_socket = [s for s in client_sockets if s != client_socket][0]
            with lock:
                activateFlag = True
            other_socket.sendall("activate".encode("utf-8"))
            break

        if message == "heartbeat":
            # If the client sends a heartbeat message, assume it's still working and update its last contact time
            client_last_contact[client_socket] = time.time()

        if message == "task":
            print(f"Client {client_ip} requested a task")
            # If the client requests a task, send it the next task in the queue if there is one
            if task_queue.empty():
                # If the task queue is empty, send a message indicating that there are no tasks available
                client_socket.send("no tasks".encode("utf-8"))
                print("No tasks available")
                no_tasks = True
                break
            else:
                # get task from queue
                task = []
                task = task_queue.get(block=False)
                task = str(task) + "\n"
                with lock:
                    current_task = task
                client_socket.send(task.encode("utf-8"))
                print("Sent task to client")
                
        elif message == "done":
            print(f"Client {client_ip} finished a task")
            ready, _, _ = select.select([client_socket], [], [], 5)
            if ready:
                # If the client indicates that it has finished a task, add the task back to the queue and update the client's last contact time
                task = []
                task = client_socket.recv(1024).decode("utf-8") 
                print(f"Put task into done queue")
                done_queue.put(task)
                task = []
                client_last_contact[client_socket] = time.time()
                #print(f"Size of work queue: {task_queue.qsize()}")
                print(f"Size of done queue: {done_queue.qsize()}")
                if done_queue.qsize() == num_tasks:
                    client_socket.send("no tasks".encode("utf-8"))
                    print("No tasks available")
                    break
            else:
                print("Client failed to send result")
        
        elif client_last_contact[client_socket] < time.time() - 5:
            # If the client has not sent a heartbeat message in 2 seconds, assume it has disconnected
            print(f"Client {client_ip} failed to send a heartbeat message assuming client is disconnected")
            client_socket.send("shut down".encode("utf-8"))
            # Activate other device
            other_socket = [s for s in client_sockets if s != client_socket][0]
            other_socket.sendall("activate".encode("utf-8"))
            with lock:
                activateFlag = True
            break

        task = []

    print("Client disconnected")
    # print(f"Current task queue contents: {list(task_queue.queue)}")
    # print(f"Activate flag: {activateFlag}")
    # if compare_results():
    #     print("Results match!")
    # else:
    #     print("Results do not match!")

def client_handler_backup(client_socket):
    no_tasks = False
    client_ip, client_port = client_socket.getsockname()
    global current_task

    while activateFlag == False:
        pass
    
    print("Backup client activated")

    while not no_tasks and activateFlag:
        # Receive messages from the client
        # ready, _, _ = select.select([client_socket], [], [], 5)
        # if ready:
        #     task = []
        message = client_socket.recv(20).decode("utf-8")
        print(f"Message from backup client {client_ip}: {message}")
        # else:
        #     print("Backup client failed to send message, RIP")
        #     break

        # if message == "heartbeat":
        #     # If the client sends a heartbeat message, assume it's still working and update its last contact time
        #     client_last_contact[client_socket] = time.time()

        if message == "task":
            print(f"Backup client {client_ip} requested a task")
            # If the client requests a task, send it the next task in the queue if there is one
            if done_queue.qsize() == num_tasks:
                # If the task queue is empty, send a message indicating that there are no tasks available
                client_socket.send("no tasks".encode("utf-8"))
                print("No tasks available")
                no_tasks = True
                break
            else:
                # if we have a current task, send it
                if current_task != []:
                        task = current_task
                        print("Backup client received current task")
                else:
                    # get task from queue
                    task = []
                    task = task_queue.get(block=False)

                task = str(task) + "\n"
                with lock:
                    current_task = task
                client_socket.send(task.encode("utf-8"))
                print("Sent task to backup client")
                
        elif message == "done":
            print(f"Backup client {client_ip} finished a task")
            ready, _, _ = select.select([client_socket], [], [], 5)
            if ready:
                # If the client indicates that it has finished a task, add the task back to the queue and update the client's last contact time
                task = []
                task = client_socket.recv(1024).decode("utf-8")
                print(f"Put task into done queue")
                done_queue.put(task)
                task = []
                client_last_contact[client_socket] = time.time()
                #print(f"Size of work queue: {task_queue.qsize()}")
                #print(f"Work queue: {list(task_queue.queue)}")
                print(f"Size of done queue: {done_queue.qsize()}")
                #print(f"Done queue: {list(done_queue.queue)}")
            else:
                print("Backup client failed to send result")
                break
        
        # elif client_last_contact[client_socket] < time.time() - 5:
        #     # If the client has not sent a heartbeat message in 2 seconds, assume it has disconnected
        #     print(f"Backup client {client_ip} failed to send a heartbeat message assuming client is disconnected")
        #     break

        task = []

    print("Backup disconnected")
    if compare_results():
        print("Results match!")
    else:
        print("Results do not match!")
    print(f"Size of done queue: {done_queue.qsize()}")
    # print(f"Size of compare queue: {compare_queue.qsize()}")
    # print(f"Done queue: {list(done_queue.queue)}")
    # print(f"Compare queue: {list(compare_queue.queue)}")



# Initialize the client sockets and last contact times
client_sockets = []
client_last_contact = {}
other_last_contact = {}

# Accept connection from main client
client_socket, address = server_socket.accept()
client_sockets.append(client_socket)
client_last_contact[client_socket] = time.time()
other_last_contact[client_socket] = time.time()
threading.Thread(target=client_handler_worker, args=(client_socket,)).start()



# Accept connection from backup client
client_socket_backup, address_backup = server_socket.accept()
client_sockets.append(client_socket_backup)
client_last_contact[client_socket_backup] = time.time()
other_last_contact[client_socket_backup] = time.time()
threading.Thread(target=client_handler_backup, args=(client_socket_backup,)).start()
