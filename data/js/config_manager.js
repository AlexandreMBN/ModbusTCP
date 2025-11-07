// JavaScript para gerenciamento de configurações JSON
// Funcionalidades de upload e download para usuário root

// Função para upload de configuração
function uploadConfig() {
    const configType = document.getElementById('configType').value;
    const configFile = document.getElementById('configFile').files[0];
    const statusDiv = document.getElementById('uploadStatus');
    
    // Validações
    if (!configType) {
        showUploadStatus('⚠️ Selecione o tipo de configuração', 'warning');
        return;
    }
    
    if (!configFile) {
        showUploadStatus('⚠️ Selecione um arquivo JSON', 'warning');
        return;
    }
    
    if (!configFile.name.toLowerCase().endsWith('.json')) {
        showUploadStatus('❌ Apenas arquivos .json são aceitos', 'error');
        return;
    }
    
    if (configFile.size > 10240) { // 10KB
        showUploadStatus('❌ Arquivo muito grande. Tamanho máximo: 10KB', 'error');
        return;
    }
    
    // Criar FormData
    const formData = new FormData();
    formData.append('configType', configType);
    formData.append('configFile', configFile);
    
    // Mostrar progresso
    showUploadStatus('⏳ Enviando configuração...', 'info');
    
    // Fazer upload
    fetch('/api/config/upload', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Extrair o nome do arquivo da mensagem se possível
            let message = data.message;
            if (message.includes('/data/config/')) {
                const fileName = message.match(/\/data\/config\/([^'\s]+)/);
                if (fileName) {
                    message = `✅ Arquivo salvo com sucesso em /data/config/${fileName[1]}`;
                }
            }
            showUploadStatus(message, 'success');
            
            // Limpar formulário após sucesso
            setTimeout(() => {
                resetUploadForm();
            }, 3000); // Aumentado para 3 segundos para ver melhor a mensagem
        } else {
            showUploadStatus(`❌ ${data.error}`, 'error');
        }
    })
    .catch(error => {
        console.error('Erro no upload:', error);
        
        // Detectar tipo de erro
        if (error.name === 'TypeError' && error.message.includes('fetch')) {
            showUploadStatus('❌ Erro de conexão com o servidor. Verifique se está conectado ao dispositivo.', 'error');
        } else if (error.message.includes('413')) {
            showUploadStatus('❌ Arquivo muito grande. Tamanho máximo permitido: 10KB', 'error');
        } else if (error.message.includes('415')) {
            showUploadStatus('❌ Tipo de arquivo não suportado. Use apenas arquivos .json', 'error');
        } else {
            showUploadStatus(`❌ Erro de comunicação: ${error.message}`, 'error');
        }
    });
}

// Função para download de configuração
function downloadConfig(configType) {
    const typeNames = {
        'rtu': 'RTU (Modbus)',
        'ap': 'WiFi Access Point',
        'sta': 'WiFi Station', 
        'mqtt': 'MQTT Client',
        'network': 'Network'
    };
    
    const url = `/api/config/download/${configType}`;
    const fileName = `${configType}_config.json`;
    
    fetch(url)
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return response.blob();
    })
    .then(blob => {
        // Criar link de download
        const a = document.createElement('a');
        const objectUrl = window.URL.createObjectURL(blob);
        a.href = objectUrl;
        a.download = fileName;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(objectUrl);
        document.body.removeChild(a);
        
        // Mostrar sucesso temporariamente
        // Tenta atualizar o botão que iniciou a ação. Se não houver (ex: chamada programática), ignora.
        const active = document.activeElement;
        if (active && active.tagName === 'BUTTON') {
            const originalText = active.innerHTML;
            active.innerHTML = '✅ Baixado!';
            active.disabled = true;
            setTimeout(() => {
                active.innerHTML = originalText;
                active.disabled = false;
            }, 2000);
        }
    })
    .catch(error => {
        console.error('Erro no download:', error);
        alert(`❌ Erro ao baixar configuração ${typeNames[configType] || configType}`);
    });
}

// Função para mostrar status do upload
function showUploadStatus(message, type) {
    const statusDiv = document.getElementById('uploadStatus');
    statusDiv.style.display = 'block';
    statusDiv.innerHTML = message;
    
    // Remover classes anteriores
    statusDiv.className = '';
    
    // Aplicar estilo baseado no tipo
    switch(type) {
        case 'success':
            statusDiv.style.backgroundColor = '#d4edda';
            statusDiv.style.color = '#155724';
            statusDiv.style.borderColor = '#c3e6cb';
            break;
        case 'error':
            statusDiv.style.backgroundColor = '#f8d7da';
            statusDiv.style.color = '#721c24';
            statusDiv.style.borderColor = '#f5c6cb';
            break;
        case 'warning':
            statusDiv.style.backgroundColor = '#fff3cd';
            statusDiv.style.color = '#856404';
            statusDiv.style.borderColor = '#ffeaa7';
            break;
        case 'info':
            statusDiv.style.backgroundColor = '#d1ecf1';
            statusDiv.style.color = '#0c5460';
            statusDiv.style.borderColor = '#bee5eb';
            break;
    }
    statusDiv.style.border = '1px solid';
    
    // Auto-hide após 5 segundos para mensagens de sucesso
    if (type === 'success') {
        setTimeout(() => {
            statusDiv.style.display = 'none';
        }, 5000);
    }
}

// Função para resetar formulário de upload
function resetUploadForm() {
    document.getElementById('configType').value = '';
    document.getElementById('configFile').value = '';
    document.getElementById('uploadStatus').style.display = 'none';
}

// Validação em tempo real do arquivo
document.addEventListener('DOMContentLoaded', function() {
    const fileInput = document.getElementById('configFile');
    if (fileInput) {
        fileInput.addEventListener('change', function(e) {
            const file = e.target.files[0];
            if (file) {
                if (!file.name.toLowerCase().endsWith('.json')) {
                    showUploadStatus('⚠️ Apenas arquivos .json são aceitos', 'warning');
                    this.value = '';
                } else if (file.size > 10240) {
                    showUploadStatus('⚠️ Arquivo muito grande. Máximo: 10KB', 'warning');
                    this.value = '';
                } else {
                    // Arquivo válido - limpar mensagens de erro
                    const statusDiv = document.getElementById('uploadStatus');
                    if (statusDiv.style.display !== 'none') {
                        statusDiv.style.display = 'none';
                    }
                }
            }
        });
    }
});

// Função utilitária para validar JSON
function isValidJSON(str) {
    try {
        JSON.parse(str);
        return true;
    } catch (e) {
        return false;
    }
}

// Configurações específicas para cada tipo
const configSchemas = {
    rtu: {
        required: ['uart_port', 'baud_rate', 'slave_address'],
        name: 'Configuração Modbus RTU'
    },
    ap: {
        required: ['ssid', 'password'],
        name: 'Configuração WiFi Access Point'
    },
    sta: {
        required: ['ssid'],
        name: 'Configuração WiFi Station'
    },
    mqtt: {
        required: ['broker_uri'],
        name: 'Configuração MQTT Client'
    },
    network: {
        required: ['hostname'],
        name: 'Configuração de Rede'
    }
};