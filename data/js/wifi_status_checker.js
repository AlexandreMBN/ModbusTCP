/**
 * WiFi Status Checker - Monitora status de conexão WiFi via polling
 */

function fetchWifiStatus() {
    fetch('/wifi_status')
        .then(function(response) {
            return response.json();
        })
        .then(function(data) {
            var statusEl = document.getElementById('conn_status');
            if (!statusEl) return;
            
            if (data.is_connected) {
                statusEl.innerHTML = '<div class="status-connected">Conectado: ' + 
                    data.current_ssid + ' - IP: ' + data.ip_address + '</div>';
            } else {
                statusEl.innerHTML = '<div class="status-waiting">Não conectado ainda. Mensagem: ' + 
                    (data.status_message || '') + '</div>';
            }
        })
        .catch(function(error) {
            console.error('Erro ao verificar status WiFi:', error);
            var statusEl = document.getElementById('conn_status');
            if (statusEl) {
                statusEl.innerHTML = '<div class="status-error">Erro ao verificar status</div>';
            }
        });
}

// Inicia o monitoramento quando a página carregar
document.addEventListener('DOMContentLoaded', function() {
    // Primeira verificação imediata
    fetchWifiStatus();
    
    // Verificação a cada 1.5 segundos
    setInterval(fetchWifiStatus, 1500);
});