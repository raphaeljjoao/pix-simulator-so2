# pix-simulator-so2

## How to Compile and Run

Use `make` to compile the project. The server and client must be run in separate terminals.

### Commands

| Command | Description |
| :--- | :--- |
| `make` | Compiles the server and client into the `bin/` directory. |
| `make clean` | Deletes all compiled files. |
| `./bin/servidor <port>` | Runs the server on the specified port. |
| `./bin/cliente <port>`| Runs a client on the specified port. |

**Example:**

```bash
# First, compile everything
make

# In Terminal 1, start the server
./bin/servidor 4000

# In Terminal 2, start a client
./bin/cliente 4000
```