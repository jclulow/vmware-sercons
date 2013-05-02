# vmware-sercons

VMware Fusion will, if appropriately coerced, emulate UARTs in guests and
expose them to the host as UNIX domain sockets.  This tool allows you to attach
your terminal emulator directly to one of those sockets without having to
lark about with ```socat``` and ```screen```.

## Build the Software

Assuming you have Xcode installed, and thus ```gcc```:

```
$ git clone git://github.com/jclulow/vmware-sercons.git
$ cd vmware-sercons
$ make
gcc -o sercons sercons.c
```

## VMware Fusion Configuration

While the GUI for VMware Fusion does not presently support adding serial
ports as sockets, you can edit the ```vmx``` file directly.  Add the following
snippet (amending for the desired socket path):

```
serial0.present = "TRUE"
serial0.fileType = "pipe"
serial0.yieldOnMsrRead = "TRUE"
serial0.startConnected = "TRUE"
serial0.fileName = "/tmp/.vmware.com1"

serial1.present = "TRUE"
serial1.fileType = "pipe"
serial1.yieldOnMsrRead = "TRUE"
serial1.startConnected = "TRUE"
serial1.fileName = "/tmp/.vmware.com2"
```

## Running the Software

Before starting up the VM, fire up ```sercons``` with the path you nominated
for the serial port you want to connect to:

```
$ sudo ./sercons /tmp/.vmware.com2
 * Waiting for socket...........
```

The program will continue to retry until a connection is made to the socket.

```
 * Waiting for socket...........
 * Connected.  Escape sequence is <CR>#.

```

You should be able to catch pretty much every byte out of the serial port on
boot this way, e.g. a grub menu, etc.

Once you're finished, press *Enter* and then *Hash*, then *Period* to end the
session.

## License

MIT
