const db = require('./db');

async function initDatabase() {
    try {
        console.log('🔧 Inicializando datos de la base de datos...');

        // Insertar las 3 plazas de estacionamiento
        await db.query(`
            INSERT INTO parking_spots (id, spot_number, is_occupied) 
            VALUES 
                (1, 1, FALSE),
                (2, 2, FALSE),
                (3, 3, FALSE)
            ON DUPLICATE KEY UPDATE spot_number=spot_number
        `);

        console.log('✅ Plazas de estacionamiento insertadas/verificadas');

        // Insertar tarifas por defecto si no existen
        const [rates] = await db.query('SELECT COUNT(*) as count FROM rates WHERE is_active = TRUE');
        if (rates[0].count === 0) {
            await db.query(`
                INSERT INTO rates (base_cost, minute_cost, is_active) 
                VALUES (5.00, 0.10, TRUE)
            `);
            console.log('✅ Tarifas por defecto insertadas');
        } else {
            console.log('ℹ️  Tarifas ya existen');
        }

        console.log('🎉 Base de datos inicializada correctamente');
        process.exit(0);
    } catch (error) {
        console.error('❌ Error inicializando base de datos:', error);
        process.exit(1);
    }
}

initDatabase();
