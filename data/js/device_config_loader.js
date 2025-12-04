/**
 * Device Config Loader
 * Carrega valores de configuração do dispositivo via API
 */

// Função para carregar configurações do dispositivo
async function loadDeviceConfig() {
    console.log('Loading device configuration from API...');
    
    try {
        const response = await fetch('/api/device_config', {
            method: 'GET',
            cache: 'no-cache',
            headers: {
                'Accept': 'application/json'
            }
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        const config = await response.json();
        console.log('Configuration loaded:', config);
        
        // Preencher campos AP
        const apSsidInput = document.querySelector('input[name="ap_ssid"]');
        const apPasswordInput = document.querySelector('input[name="ap_password"]');
        const apIpInput = document.querySelector('input[name="ap_ip"]');
        
        if (apSsidInput && config.ap_ssid) {
            apSsidInput.value = config.ap_ssid;
        }
        
        if (apPasswordInput && config.ap_password) {
            apPasswordInput.value = config.ap_password;
        }
        
        if (apIpInput && config.ap_ip) {
            apIpInput.value = config.ap_ip;
        }
        
        // Preencher campos RTU
        const rtuSlaveInput = document.querySelector('input[name="rtu_slave_address"]');
        const rtuTimeoutInput = document.querySelector('input[name="rtu_timeout"]');
        
        if (rtuSlaveInput && config.rtu_slave_address !== undefined) {
            rtuSlaveInput.value = config.rtu_slave_address;
        }
        
        if (rtuTimeoutInput && config.rtu_timeout !== undefined) {
            rtuTimeoutInput.value = config.rtu_timeout;
        }
        
        console.log('Form fields populated successfully');
        
    } catch (error) {
        console.error('Error loading device configuration:', error);
        // Não mostrar alert para não interromper a experiência do usuário
        // Os campos ficarão com valores vazios ou padrão do HTML
    }
}

// Carregar configurações quando a página terminar de carregar
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', loadDeviceConfig);
} else {
    // DOMContentLoaded já disparou
    loadDeviceConfig();
}
