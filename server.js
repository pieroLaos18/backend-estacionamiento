const express = require('express');
const cors = require('cors');
const db = require('./db');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3001;

// Middleware
app.use(cors());
app.use(express.json());

// ==========================================
// RUTAS API
// ==========================================

// --- TARIFAS ---

// Obtener tarifas activas
app.get('/api/rates', async (req, res) => {
    try {
        const [rows] = await db.query('SELECT * FROM rates WHERE is_active = TRUE ORDER BY id DESC LIMIT 1');
        if (rows.length > 0) {
            res.json({
                base: parseFloat(rows[0].base_cost),
                minute: parseFloat(rows[0].minute_cost)
            });
        } else {
            // Default si no hay nada en BD
            res.json({ base: 5.00, minute: 0.10 });
        }
    } catch (error) {
        console.error('Error fetching rates:', error);
        res.status(500).json({ error: 'Error interno del servidor' });
    }
});

// Actualizar tarifas
app.post('/api/rates', async (req, res) => {
    const { base, minute } = req.body;
    try {
        // Desactivar tarifas anteriores
        await db.query('UPDATE rates SET is_active = FALSE WHERE is_active = TRUE');
        // Insertar nueva tarifa
        await db.query('INSERT INTO rates (base_cost, minute_cost, is_active) VALUES (?, ?, TRUE)', [base, minute]);
        res.json({ success: true, message: 'Tarifas actualizadas' });
    } catch (error) {
        console.error('Error updating rates:', error);
        res.status(500).json({ error: 'Error actualizando tarifas' });
    }
});

// --- VEHÍCULOS ACTIVOS ---

// Obtener vehículos activos (reemplaza localStorage)
app.get('/api/vehicles/active', async (req, res) => {
    try {
        // Obtener sesiones con status 'active'
        const [rows] = await db.query(`
            SELECT 
                plate, 
                spot_id as spotId, 
                UNIX_TIMESTAMP(entry_time) * 1000 as entryTime,
                UNIX_TIMESTAMP(exit_time) * 1000 as exitTime
            FROM parking_sessions 
            WHERE status = 'active'
        `);
        res.json(rows);
    } catch (error) {
        console.error('Error fetching active vehicles:', error);
        res.status(500).json({ error: 'Error obteniendo vehículos' });
    }
});

// Registrar entrada (desde sensor/manual)
app.post('/api/vehicles/entry', async (req, res) => {
    const { plate, spotId } = req.body;
    try {
        // Verificar si ya existe activo en esa plaza
        const [existing] = await db.query('SELECT id FROM parking_sessions WHERE spot_id = ? AND status = "active"', [spotId]);
        if (existing.length > 0) {
            return res.status(400).json({ success: false, msg: 'Plaza ocupada' });
        }

        // Insertar sesión en parking_sessions
        await db.query('INSERT INTO parking_sessions (plate, spot_id, status) VALUES (?, ?, "active")', [plate, spotId]);

        // Actualizar tabla parking_spots
        await db.query(`
            UPDATE parking_spots 
            SET is_occupied = TRUE, 
                current_vehicle_plate = ?, 
                last_status_change = CURRENT_TIMESTAMP 
            WHERE id = ?
        `, [plate, spotId]);

        res.json({ success: true, msg: 'Entrada registrada' });
    } catch (error) {
        console.error('Error registering entry:', error);
        res.status(500).json({ error: 'Error registrando entrada' });
    }
});

// Registrar salida (pago)
app.post('/api/vehicles/exit', async (req, res) => {
    const { plate, cost, totalTime } = req.body;
    try {
        // Obtener spot_id antes de completar la sesión
        const [session] = await db.query('SELECT spot_id FROM parking_sessions WHERE plate = ? AND status = "active"', [plate]);

        if (session.length === 0) {
            return res.status(404).json({ success: false, msg: 'Vehículo no encontrado' });
        }

        const spotId = session[0].spot_id;

        // Actualizar sesión
        await db.query(`
            UPDATE parking_sessions 
            SET 
                status = 'completed', 
                exit_time = CURRENT_TIMESTAMP,
                total_cost = ?,
                total_time_minutes = ?
            WHERE plate = ? AND status = 'active'
        `, [cost, totalTime, plate]);

        // Liberar plaza en parking_spots
        await db.query(`
            UPDATE parking_spots 
            SET is_occupied = FALSE, 
                current_vehicle_plate = NULL, 
                last_status_change = CURRENT_TIMESTAMP 
            WHERE id = ?
        `, [spotId]);

        res.json({ success: true, msg: 'Salida registrada y pagada' });
    } catch (error) {
        console.error('Error registering exit:', error);
        res.status(500).json({ error: 'Error registrando salida' });
    }
});

// Marcar hora de salida (pre-pago, para detener timer)
app.post('/api/vehicles/mark-exit', async (req, res) => {
    const { plate } = req.body;
    try {
        // Solo actualizamos exit_time si está nulo, mantenemos status active hasta pagar
        await db.query(`
            UPDATE parking_sessions 
            SET exit_time = CURRENT_TIMESTAMP 
            WHERE plate = ? AND status = 'active' AND exit_time IS NULL
        `, [plate]);
        res.json({ success: true });
    } catch (error) {
        console.error('Error marking exit time:', error);
        res.status(500).json({ error: 'Error marcando salida' });
    }
});


// --- DASHBOARD & HISTORIAL ---

// Obtener métricas para el Dashboard
app.get('/api/dashboard', async (req, res) => {
    try {
        const today = new Date().toISOString().split('T')[0];
        const currentMonth = new Date().getMonth() + 1;
        const currentYear = new Date().getFullYear();

        // 1. Ganancias Hoy
        const [todayRows] = await db.query(`
            SELECT COALESCE(SUM(total_cost), 0) as total 
            FROM parking_sessions 
            WHERE status = 'completed' AND DATE(exit_time) = CURDATE()
        `);

        // 2. Ganancias Mes
        const [monthRows] = await db.query(`
            SELECT COALESCE(SUM(total_cost), 0) as total 
            FROM parking_sessions 
            WHERE status = 'completed' AND MONTH(exit_time) = ? AND YEAR(exit_time) = ?
        `, [currentMonth, currentYear]);

        // 3. Tiempo Promedio (minutos)
        const [avgRows] = await db.query(`
            SELECT COALESCE(AVG(total_time_minutes), 0) as avgTime 
            FROM parking_sessions 
            WHERE status = 'completed'
        `);

        // 4. Mejor Mes (Ganancias)
        const [bestMonthRows] = await db.query(`
            SELECT 
                DATE_FORMAT(exit_time, '%M %Y') as month, 
                SUM(total_cost) as total 
            FROM parking_sessions 
            WHERE status = 'completed'
            GROUP BY DATE_FORMAT(exit_time, '%M %Y'), YEAR(exit_time), MONTH(exit_time)
            ORDER BY total DESC 
            LIMIT 1
        `);

        res.json({
            earningsToday: parseFloat(todayRows[0].total) || 0,
            earningsMonth: parseFloat(monthRows[0].total) || 0,
            averageTime: Math.round(avgRows[0].avgTime || 0),
            bestMonth: bestMonthRows.length > 0 ? bestMonthRows[0].month : "N/A"
        });
    } catch (error) {
        console.error('Error fetching dashboard metrics:', error);
        res.status(500).json({ error: 'Error obteniendo métricas' });
    }
});

// Obtener historial completo (o limitado)
app.get('/api/history', async (req, res) => {
    try {
        const [rows] = await db.query(`
            SELECT 
                plate, 
                UNIX_TIMESTAMP(entry_time) * 1000 as entryTime,
                UNIX_TIMESTAMP(exit_time) * 1000 as exitTime,
                total_time_minutes as totalTime,
                total_cost as cost,
                spot_id as spotId
            FROM parking_sessions 
            WHERE status = 'completed' 
            ORDER BY exit_time DESC 
            LIMIT 50
        `);
        res.json(rows);
    } catch (error) {
        console.error('Error fetching history:', error);
        res.status(500).json({ error: 'Error obteniendo historial' });
    }
});

// --- COLA DE ENTRADA ---

// Obtener cola de espera
app.get('/api/queue', async (req, res) => {
    try {
        const [rows] = await db.query('SELECT * FROM entry_queue WHERE status = "waiting" ORDER BY arrival_time ASC');
        res.json(rows);
    } catch (error) {
        console.error('Error fetching queue:', error);
        res.status(500).json({ error: 'Error obteniendo cola' });
    }
});

// Agregar a la cola
app.post('/api/queue', async (req, res) => {
    const { plate } = req.body;
    try {
        // Verificar duplicados en cola o activos
        const [existingQueue] = await db.query('SELECT id FROM entry_queue WHERE plate = ? AND status = "waiting"', [plate]);
        const [existingActive] = await db.query('SELECT id FROM parking_sessions WHERE plate = ? AND status = "active"', [plate]);

        if (existingQueue.length > 0 || existingActive.length > 0) {
            return res.status(400).json({ success: false, msg: 'Vehículo ya registrado' });
        }

        await db.query('INSERT INTO entry_queue (plate) VALUES (?)', [plate]);
        res.json({ success: true, msg: 'Agregado a la cola' });
    } catch (error) {
        console.error('Error adding to queue:', error);
        res.status(500).json({ error: 'Error agregando a cola' });
    }
});

// Remover de la cola (cancelar)
app.delete('/api/queue/:id', async (req, res) => {
    const { id } = req.params;
    try {
        await db.query('DELETE FROM entry_queue WHERE id = ?', [id]);
        res.json({ success: true, msg: 'Removido de la cola' });
    } catch (error) {
        console.error('Error removing from queue:', error);
        res.status(500).json({ error: 'Error removiendo de cola' });
    }
});

// Marcar como asignado (al entrar a plaza)
app.post('/api/queue/:id/assign', async (req, res) => {
    const { id } = req.params;
    try {
        await db.query('UPDATE entry_queue SET status = "assigned" WHERE id = ?', [id]);
        res.json({ success: true });
    } catch (error) {
        console.error('Error assigning queue item:', error);
        res.status(500).json({ error: 'Error asignando' });
    }
});

// Iniciar servidor
app.listen(PORT, () => {
    console.log(`🚀 Servidor backend corriendo en http://localhost:${PORT}`);
});
