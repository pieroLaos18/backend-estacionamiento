const mysql = require('mysql2');
require('dotenv').config();

// Crear pool de conexiones para mejor rendimiento
const pool = mysql.createPool({
    host: process.env.DB_HOST || 'localhost',
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASSWORD || '',
    database: process.env.DB_NAME || 'parking_system',
    waitForConnections: true,
    connectionLimit: 10,
    queueLimit: 0
});

// Promisify para usar async/await
const promisePool = pool.promise();

// Verificar conexión inicial
pool.getConnection((err, connection) => {
    if (err) {
        console.error('❌ Error conectando a la base de datos:', err.code, err.message);
    } else {
        console.log('✅ Conectado exitosamente a MySQL Database');
        connection.release();
    }
});

module.exports = promisePool;
