// Funções para monitoramento do status WiFi
let statusCheckInterval = null;

document.addEventListener('DOMContentLoaded', function() {
    // Se está em processo de conexão, inicia verificação automática
    const progressDiv = document.getElementById('connection-progress');
    if (progressDiv && progressDiv.style.display !== 'none') {
        startStatusChecking();
    }
});

function checkStatus() {
    fetch('/wifi-status-data')
        .then(response => response.json())
        .then(data => {
            updateStatusDisplay(data);
        })
        .catch(error => {
            console.error('Erro ao verificar status:', error);
            showError('Erro ao verificar status da conexão');
        });
}

function updateStatusDisplay(data) {
    const statusSpan = document.getElementById('wifi-status');
    const ssidSpan = document.getElementById('wifi-ssid');
    const ipSection = document.getElementById('ip-section');
    const rssiSection = document.getElementById('rssi-section');
    const progressDiv = document.getElementById('connection-progress');
    const errorDiv = document.getElementById('error-message');
    
    // Limpa displays anteriores
    progressDiv.style.display = 'none';
    errorDiv.style.display = 'none';
    ipSection.style.display = 'none';
    rssiSection.style.display = 'none';
    
    // Atualiza status
    statusSpan.textContent = data.status || 'Desconhecido';
    statusSpan.className = 'status-badge ' + getStatusClass(data.status);
    
    ssidSpan.textContent = data.ssid || 'Nenhuma rede';
    
    if (data.status === 'Conectado') {
        if (data.ip) {
            document.getElementById('wifi-ip').textContent = data.ip;
            ipSection.style.display = 'block';
        }
        if (data.rssi) {
            document.getElementById('wifi-rssi').textContent = data.rssi;
            rssiSection.style.display = 'block';
        }
        stopStatusChecking();
    } else if (data.status === 'Conectando') {
        progressDiv.style.display = 'block';
        if (!statusCheckInterval) {
            startStatusChecking();
        }
    } else if (data.status === 'Erro' || data.status === 'Falha') {
        showError(data.message || 'Falha na conexão WiFi');
        stopStatusChecking();
    }
}

function getStatusClass(status) {
    switch(status) {
        case 'Conectado': return 'status-connected';
        case 'Conectando': return 'status-connecting';
        case 'Erro':
        case 'Falha': return 'status-error';
        default: return 'status-disconnected';
    }
}

function showError(message) {
    const errorDiv = document.getElementById('error-message');
    errorDiv.innerHTML = `<p class="error">${message}</p>`;
    errorDiv.style.display = 'block';
}

function startStatusChecking() {
    if (statusCheckInterval) return;
    
    statusCheckInterval = setInterval(function() {
        checkStatus();
    }, 3000); // Verifica a cada 3 segundos
    
    // Para após 30 segundos para evitar loop infinito
    setTimeout(function() {
        stopStatusChecking();
    }, 30000);
}

function stopStatusChecking() {
    if (statusCheckInterval) {
        clearInterval(statusCheckInterval);
        statusCheckInterval = null;
    }
}