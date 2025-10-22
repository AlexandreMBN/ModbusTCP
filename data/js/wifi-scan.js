// Funções para gerenciamento da tela de scan WiFi
document.addEventListener('DOMContentLoaded', function() {
    // Inicia o scan automático
    setTimeout(function() {
        loadWifiNetworks();
    }, 2000);
});

function loadWifiNetworks() {
    fetch('/wifi-scan-data')
        .then(response => response.json())
        .then(data => {
            if (data.networks && data.networks.length > 0) {
                populateWifiSelect(data.networks);
                document.getElementById('scan-status').style.display = 'none';
                document.getElementById('wifi-form').style.display = 'block';
            } else {
                document.getElementById('scan-status').innerHTML = '<p>Nenhuma rede encontrada. <button onclick="rescanWifi()">Tentar novamente</button></p>';
            }
        })
        .catch(error => {
            console.error('Erro ao carregar redes:', error);
            document.getElementById('scan-status').innerHTML = '<p>Erro ao escanear redes. <button onclick="rescanWifi()">Tentar novamente</button></p>';
        });
}

function populateWifiSelect(networks) {
    const select = document.getElementById('wifi-select');
    select.innerHTML = '';
    
    networks.forEach(network => {
        const option = document.createElement('option');
        option.value = network.ssid;
        option.textContent = `${network.ssid} (${network.rssi} dBm)`;
        select.appendChild(option);
    });
}

function rescanWifi() {
    document.getElementById('scan-status').style.display = 'block';
    document.getElementById('scan-status').innerHTML = '<p>Escaneando redes WiFi...</p>';
    document.getElementById('wifi-form').style.display = 'none';
    
    // Força um novo scan
    fetch('/wifi-scan-trigger', { method: 'POST' })
        .then(() => {
            setTimeout(loadWifiNetworks, 3000);
        })
        .catch(error => {
            console.error('Erro ao iniciar scan:', error);
        });
}

// Submissão do formulário
document.getElementById('wifi-form').addEventListener('submit', function(e) {
    e.preventDefault();
    
    const formData = new FormData(this);
    const data = new URLSearchParams(formData);
    
    fetch('/wifi-save', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: data
    })
    .then(response => response.text())
    .then(result => {
        alert('Configuração salva! O ESP32 tentará conectar à rede.');
        window.location.href = '/wifi-status';
    })
    .catch(error => {
        console.error('Erro ao salvar:', error);
        alert('Erro ao salvar configuração.');
    });
});