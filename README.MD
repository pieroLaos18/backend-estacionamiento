# Backend - Sistema de Estacionamiento

Backend para el sistema de gestión de estacionamiento con integración MQTT para sensores ESP32 y API REST para el frontend.

## 🚀 Tecnologías

- **Node.js** + **Express** - Servidor y API REST
- **MySQL** - Base de datos relacional
- **MQTT** - Comunicación con sensores ESP32 (HiveMQ)
- **dotenv** - Gestión de variables de entorno
- **CORS** - Habilitación de peticiones cross-origin

---

## 📋 Requisitos Previos

Antes de comenzar, asegúrate de tener instalado:

- [Node.js](https://nodejs.org/) (v14 o superior)
- [MySQL](https://dev.mysql.com/downloads/mysql/) (v8.0 o superior)
- [Git](https://git-scm.com/downloads)

---

## ⚙️ Instalación

### 1. Clonar el repositorio

```bash
git clone https://github.com/pieroLaos18/backend-estacionamiento.git
cd backend-estacionamiento
```

### 2. Instalar dependencias

```bash
npm install
```

### 3. Configurar variables de entorno

Copia el archivo `.env_example` y renómbralo a `.env`:

```bash
cp .env_example .env
```

Luego edita el archivo `.env` con tus credenciales:

```env
# Configuración de Base de Datos MySQL
DB_HOST=localhost
DB_USER=root
DB_PASSWORD=tu_password_mysql
DB_NAME=parking_system
DB_PORT=3306

# Puerto del servidor
PORT=3001

# Configuración MQTT (HiveMQ Cloud)
MQTT_BROKER_URL=wss://tu-broker.hivemq.cloud:8884/mqtt
MQTT_USER=tu_usuario_mqtt
MQTT_PASSWORD=tu_password_mqtt
```

### 4. Crear la base de datos

Ejecuta el script de inicialización de la base de datos:

```bash
node init-db.js
```

Este script creará:
- La base de datos `parking_system`
- Las tablas necesarias: `rates`, `parking_sessions`, `entry_queue`, `parking_spots`
- Datos iniciales de ejemplo

---

## 🏃 Ejecución

### Iniciar el servidor

```bash
node server.js
```

El servidor estará corriendo en: `http://localhost:3001`

Deberías ver el mensaje:
```
🚀 Servidor backend corriendo en http://localhost:3001
```

---

## 📡 API Endpoints

### **Tarifas**

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/api/rates` | Obtener tarifas activas |
| `POST` | `/api/rates` | Actualizar tarifas |

### **Vehículos**

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/api/vehicles/active` | Obtener vehículos actualmente estacionados |
| `POST` | `/api/vehicles/entry` | Registrar entrada de vehículo |
| `POST` | `/api/vehicles/exit` | Registrar salida y pago |
| `POST` | `/api/vehicles/mark-exit` | Marcar hora de salida (pre-pago) |

### **Dashboard**

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/api/dashboard` | Obtener métricas del dashboard |
| `GET` | `/api/history` | Obtener historial de sesiones |

### **Cola de Entrada**

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| `GET` | `/api/queue` | Obtener cola de espera |
| `POST` | `/api/queue` | Agregar vehículo a la cola |
| `DELETE` | `/api/queue/:id` | Remover vehículo de la cola |
| `POST` | `/api/queue/:id/assign` | Marcar vehículo como asignado |

---

## 📁 Estructura del Proyecto

```
backend/
├── _esp32_/              # Código para sensores ESP32
├── node_modules/         # Dependencias (ignorado en git)
├── .env                  # Variables de entorno (ignorado en git)
├── .env_example          # Ejemplo de variables de entorno
├── .gitignore            # Archivos ignorados por git
├── db.js                 # Configuración de conexión a MySQL
├── init-db.js            # Script de inicialización de BD
├── package.json          # Dependencias y scripts npm
├── server.js             # Servidor Express y rutas API
└── README.MD             # Este archivo
```

---

## 🗄️ Esquema de Base de Datos

### Tabla: `rates`
Almacena las tarifas del estacionamiento.

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `id` | INT | ID autoincremental |
| `base_cost` | DECIMAL(10,2) | Costo base |
| `minute_cost` | DECIMAL(10,2) | Costo por minuto |
| `is_active` | BOOLEAN | Si está activa |
| `created_at` | TIMESTAMP | Fecha de creación |

### Tabla: `parking_sessions`
Registra las sesiones de estacionamiento.

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `id` | INT | ID autoincremental |
| `plate` | VARCHAR(20) | Placa del vehículo |
| `spot_id` | INT | ID de la plaza |
| `entry_time` | TIMESTAMP | Hora de entrada |
| `exit_time` | TIMESTAMP | Hora de salida |
| `total_cost` | DECIMAL(10,2) | Costo total |
| `total_time_minutes` | INT | Tiempo total en minutos |
| `status` | ENUM | `active` o `completed` |

### Tabla: `entry_queue`
Cola de espera de vehículos.

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `id` | INT | ID autoincremental |
| `plate` | VARCHAR(20) | Placa del vehículo |
| `arrival_time` | TIMESTAMP | Hora de llegada |
| `status` | ENUM | `waiting` o `assigned` |

### Tabla: `parking_spots`
Estado de las plazas de estacionamiento.

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `id` | INT | ID de la plaza |
| `is_occupied` | BOOLEAN | Si está ocupada |
| `current_vehicle_plate` | VARCHAR(20) | Placa del vehículo actual |
| `last_status_change` | TIMESTAMP | Última actualización |

---

## 🔧 Solución de Problemas

### Error: "Cannot connect to MySQL"
- Verifica que MySQL esté corriendo
- Confirma las credenciales en `.env`
- Asegúrate de que la base de datos `parking_system` exista

### Error: "Port 3001 already in use"
- Cambia el puerto en `.env` a otro disponible (ej: `PORT=3002`)

### Error: "MQTT connection failed"
- Verifica las credenciales de HiveMQ en `.env`
- Confirma que el broker MQTT esté accesible

---

## 👥 Contribución

1. Haz un fork del proyecto
2. Crea una rama para tu feature (`git checkout -b feature/nueva-funcionalidad`)
3. Commit tus cambios (`git commit -m 'Agregar nueva funcionalidad'`)
4. Push a la rama (`git push origin feature/nueva-funcionalidad`)
5. Abre un Pull Request

---

## 📄 Licencia

ISC

---

## 📞 Contacto

Para preguntas o soporte, contacta al equipo de desarrollo.
