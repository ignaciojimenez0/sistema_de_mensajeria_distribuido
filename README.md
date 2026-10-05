### Descripción
Sistema de mensajería distribuido con transferencia de archivos y persistencia de datos. Tecnologías: Python, C, sockets TCP, RPC, SOAP, Docker

# Cómo compilar y ejecutar 

Se incluyen los siguientes archivos:
- client.py ⇒ incluye el código del cliente
- Makefile ⇒ para compilar de forma rápida y sencilla mediante make
- message_conversor.py ⇒ incluye el código del servidor conversor de mensajes
- README.md ⇒ con instrucciones de compilación y ejecución
- server.c ⇒ incluye el código del servidor principal
- servidor_logs.x ⇒ incluye la interfaz del servidor de logs. Gracias a este fichero se genera mediante un rpcgen en el Makefile los stubs y todos los archivos adicionales de XDR
- servidor_logs_proxy.c ⇒ incluye el código del proxy para comunicarse con el servidor de logs. Se compila como una librería dinámica que se vincula dinámicamente al código del servidor principal
- servidor_logs_server.c ⇒ incluye el código del servidor de logs
Como se puede apreciar, hemos incluido un Makefile para facilitar la compilación basándonos en los realizados en los ejercicios evaluables

#### Compilación
Para compilar se debe realizar:

```
make clean
make
```

para eliminar posibles archivos auxiliares antiguos y volver a compilar y generar automáticamente todos los ficheros necesarios

#### Ejecución
Para ejecutar mediante contenedores Docker tal y como se nos exige en el enunciado debemos:

1. Clonar el repositorio de Git en el que se nos da los archivos necesarios para crear y utilizar los contenedores Docker en la misma carpeta donde tenemos todos los archivos del zip descomprimido. Otra buena práctica es eliminar la carpeta user_files en la que se generan los ficheros de información del servidor principal, de esta forma nos aseguramos que no queden usuarios creados de pruebas anteriores. También debemos asegurarnos de que el daemon de Docker esté en ejecución

2. Si queremos simular K usuarios, debemos lanzar 2 * K + 2 terminales. Esto es porque el servidor principal necesita una terminal, al igual que el servidor de logs, y por cada cliente se necesitan dos terminales: una para el conversor de mensajes y otra para la interfaz por línea de comandos para que el usuario pueda utilizar la aplicación

3. En una terminal cualquiera vamos a ejecutar make clean y después make para compilar

4. Si queremos simular K usuarios, debemos lanzar K + 2 contenedores Docker mediante. Esto es porque el servidor principal necesita su propio contenedor Docker, al igual que el servidor de logs, y por cada cliente se necesita un contenedor, ya que el conversor de mensajes se utiliza de forma local a la interfaz por línea de comandos. Para lanzar, por ejemplo, 3 contenedores Docker se debe hacer
```
./u22.sh start 3
```

5. El siguiente paso es entrar en los contenedores en cada terminal. Vamos a suponer que vamos a simular 2 usuarios, por lo que ya hemos abierto 2 * 2 + 2 = 6 terminales y hemos ejecutado ./u22.sh start 4. En una terminal debemos entrar a un contenedor, por ejemplo el 1, donde se ejecutará el servidor principal. En otra terminal debemos entrar a otro contenedor, por ejemplo el 3, donde se ejecutará el servidor de logs. Ahora nos restan 4 terminales sin utilizar y los contenedores 2 y 4 sin utilizar, así que vamos a entrar con dos terminales al contenedor 2 y con otras dos al contenedor 4. Para entrar al contenedor 3, por ejemplo, se debe hacer:
```
./u22.sh bash 3
```

6. El siguiente paso consiste en arrancar el servidor de logs. Pero antes debemos averiguar la dirección IP del contenedor Docker en el que se está ejecutando, que nos será necesaria más adelante. Para ello, en uno de los contenedores que se ha abierto en una sola terminal, se ejecuta:
```
hostname -I
./logs_server
```

7. El siguiente paso consiste en arrancar el servidor principal. Se le debe pasar como argumento un puerto entre 1024 y 65535. Por ejemplo, elegiremos el puerto 8080. Además, debemos añadir como variable de entorno la dirección IP del contenedor donde se está ejecutando el servidor de logs obtenida anteriormente. Para ello, en el otro contenedor Docker que se ha abierto en una sola terminal, se ejecuta:
```
export LOG_RPC_IP= dirección IP obtenida en el paso anterior
./server -p puerto
```

8. El siguiente paso consiste en arrancar los conversores de mensajes. Siguiendo el ejemplo anterior, nos quedan dos terminales sin usar con el contenedor Docker 2 abierto, y otras dos terminales con el contenedor 4 en ambas. Para arrancar los conversores de mensajes, en una terminal de cada contenedor (es decir, en una terminal con el contenedor 2 y otra con el contenedor 4) se debe ejecutar:
```
python3 message_conversor.py
```

9. El último paso consiste en arrancar los clientes mediante los cuales los usuarios podrán conectarse al servidor y enviar mensajes a otros usuarios. Debemos pasarle como argumento la dirección IP del contenedor Docker en el que se está ejecutando el servidor principal y puerto que le pasamos como argumento al servidor principal. La dirección IP y el puerto del servidor principal se obtienen de forma muy sencilla, ya que al arrancar el servidor en el paso 7, se imprime server init dirección_IP:puerto, de donde podemos obtener estos datos. Siguiendo el ejemplo anterior, nos quedan una terminal sin usar con el contenedor Docker 2 abierto, y otra con el contenedor 4. Para arrancar los clientes, en las terminales restantes (es decir, en una terminal con el contenedor 2 y otra con el contenedor 4) se debe ejecutar:
```
python3 client.py -s ip_del_servidor_principal -p puerto_del_servidor_principal
```


Una vez seguidos todos estos pasos ya se encuentra el sistema arrancado por completo y podemos utilizar nuestros clientes con normalidad
