/*
 * ============================================================================
 * JAVASCRIPT CENTRALIZADO - SISTEMA DE MEDIÇÃO DE COMBUSTÃO
 * ============================================================================
 */

/**
 * Configuração global do sistema
 */
const AppConfig = {
    // Timeouts em milissegundos
    AJAX_TIMEOUT: 10000,
    SCAN_TIMEOUT: 30000,
    
    // Mensagens padrão
    MESSAGES: {
        CONFIRM_FACTORY_RESET: 'Tem certeza que deseja realizar o reset de fabrica?\n\nEsta acao apagara todas as configuracoes!',
        FACTORY_RESET_SUCCESS: 'Reset iniciado! O dispositivo sera reiniciado.',
        WIFI_SCANNING: 'Escaneando redes Wi-Fi...',
        WIFI_SCAN_ERROR: 'Erro ao escanear redes Wi-Fi. Tente novamente.',
        SAVE_SUCCESS: 'Configurações salvas com sucesso!',
        SAVE_ERROR: 'Erro ao salvar configurações. Tente novamente.',
        VALIDATION_ERROR: 'Por favor, corrija os campos marcados em vermelho.'
    }
};

/**
 * ============================================================================
 * UTILITÁRIOS GERAIS
 * ============================================================================
 */

/**
 * Exibe uma notificação toast
 * @param {string} message - Mensagem a ser exibida
 * @param {string} type - Tipo: 'success', 'error', 'warning', 'info'
 */
function showToast(message, type = 'info') {
    // Remove toast anterior se existir
    const existingToast = document.querySelector('.toast');
    if (existingToast) {
        existingToast.remove();
    }

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.innerHTML = `
        <span>${message}</span>
        <button onclick="this.parentElement.remove()">&times;</button>
    `;

    // Adiciona estilos inline para o toast
    toast.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 12px 20px;
        border-radius: 8px;
        color: white;
        font-weight: bold;
        z-index: 10000;
        max-width: 300px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.3);
        display: flex;
        justify-content: space-between;
        align-items: center;
        animation: slideInRight 0.3s ease;
    `;

    // Cores por tipo
    const colors = {
        success: '#27ae60',
        error: '#e74c3c',
        warning: '#f39c12',
        info: '#3498db'
    };
    toast.style.backgroundColor = colors[type] || colors.info;

    document.body.appendChild(toast);

    // Remove automaticamente após 5 segundos
    setTimeout(() => {
        if (toast.parentElement) {
            toast.remove();
        }
    }, 5000);
}

/**
 * Valida um endereço de email
 * @param {string} email - Email a ser validado
 * @returns {boolean}
 */
function validateEmail(email) {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    return emailRegex.test(email);
}

/**
 * Valida um endereço IP
 * @param {string} ip - IP a ser validado
 * @returns {boolean}
 */
function validateIP(ip) {
    const ipRegex = /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
    return ipRegex.test(ip);
}

/**
 * Valida uma porta de rede
 * @param {number} port - Porta a ser validada
 * @returns {boolean}
 */
function validatePort(port) {
    return Number.isInteger(port) && port >= 1 && port <= 65535;
}

/**
 * Sanitiza input para prevenir XSS
 * @param {string} str - String a ser sanitizada
 * @returns {string}
 */
function sanitizeInput(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

/**
 * ============================================================================
 * FUNÇÕES ESPECÍFICAS DO SISTEMA
 * ============================================================================
 */

/**
 * Realiza o reset de fábrica
 */
function doFactoryReset() {
    if (confirm(AppConfig.MESSAGES.CONFIRM_FACTORY_RESET)) {
        showToast('Executando reset de fábrica...', 'warning');
        
        fetch('/factory_reset', { method: 'POST' })
            .then(response => {
                if (response.ok) {
                    showToast(AppConfig.MESSAGES.FACTORY_RESET_SUCCESS, 'success');
                    // Redireciona para a página inicial após 3 segundos
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 3000);
                } else {
                    throw new Error('Erro na resposta do servidor');
                }
            })
            .catch(error => {
                console.error('Erro no reset de fábrica:', error);
                showToast('Erro ao executar reset. Tente novamente.', 'error');
            });
    }
}

/**
 * Escaneia redes Wi-Fi disponíveis
 */
function scanWifiNetworks() {
    const button = document.querySelector('#scan-wifi-btn');
    const networksContainer = document.querySelector('#wifi-networks-list');
    
    if (button) {
        button.disabled = true;
        button.textContent = AppConfig.MESSAGES.WIFI_SCANNING;
    }
    
    showToast(AppConfig.MESSAGES.WIFI_SCANNING, 'info');
    
    fetch('/wifi_scan')
        .then(response => response.json())
        .then(data => {
            if (networksContainer && data.networks) {
                displayWifiNetworks(data.networks, networksContainer);
                showToast(`${data.networks.length} redes encontradas`, 'success');
            }
        })
        .catch(error => {
            console.error('Erro no scan Wi-Fi:', error);
            showToast(AppConfig.MESSAGES.WIFI_SCAN_ERROR, 'error');
        })
        .finally(() => {
            if (button) {
                button.disabled = false;
                button.textContent = 'Escanear Redes';
            }
        });
}

/**
 * Exibe as redes Wi-Fi na interface
 * @param {Array} networks - Lista de redes
 * @param {Element} container - Container para exibir as redes
 */
function displayWifiNetworks(networks, container) {
    container.innerHTML = '';
    
    networks.forEach((network, index) => {
        const networkItem = document.createElement('div');
        networkItem.className = 'wifi-item';
        networkItem.innerHTML = `
            <input type="radio" name="selected_network" value="${sanitizeInput(network.ssid)}" id="network_${index}">
            <label for="network_${index}">
                ${sanitizeInput(network.ssid)} 
                <span style="color: #666; font-size: 0.9em;">(${network.rssi} dBm)</span>
            </label>
        `;
        
        networkItem.addEventListener('click', () => {
            const radio = networkItem.querySelector('input[type="radio"]');
            radio.checked = true;
            
            // Preenche o campo SSID se existir
            const ssidInput = document.querySelector('input[name="ssid"]');
            if (ssidInput) {
                ssidInput.value = network.ssid;
            }
        });
        
        container.appendChild(networkItem);
    });
}

// (Tema removido nesta versão)

/**
 * Testa conexão Wi-Fi
 * @param {string} ssid - Nome da rede
 * @param {string} password - Senha da rede
 */
function testWifiConnection(ssid, password) {
    showToast('Testando conexão Wi-Fi...', 'info');
    
    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('password', password);
    
    fetch('/wifi_test_connect', {
        method: 'POST',
        body: formData
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showToast('Conexão Wi-Fi bem-sucedida!', 'success');
            } else {
                showToast(`Erro na conexão: ${data.error || 'Erro desconhecido'}`, 'error');
            }
        })
        .catch(error => {
            console.error('Erro no teste de conexão:', error);
            showToast('Erro ao testar conexão Wi-Fi', 'error');
        });
}

/**
 * Valida formulário em tempo real
 * @param {HTMLFormElement} form - Formulário a ser validado
 */
function setupFormValidation(form) {
    const inputs = form.querySelectorAll('input, select, textarea');
    
    inputs.forEach(input => {
        input.addEventListener('blur', () => validateField(input));
        input.addEventListener('input', () => clearFieldError(input));
    });
}

/**
 * Valida um campo específico
 * @param {HTMLInputElement} field - Campo a ser validado
 */
function validateField(field) {
    const value = field.value.trim();
    const type = field.type;
    const required = field.hasAttribute('required');
    let isValid = true;
    let errorMessage = '';
    
    // Verifica se campo obrigatório está vazio
    if (required && !value) {
        isValid = false;
        errorMessage = 'Este campo é obrigatório';
    }
    // Validações específicas por tipo
    else if (value) {
        switch (type) {
            case 'email':
                if (!validateEmail(value)) {
                    isValid = false;
                    errorMessage = 'Email inválido';
                }
                break;
            case 'number':
                const num = parseFloat(value);
                const min = field.getAttribute('min');
                const max = field.getAttribute('max');
                
                if (isNaN(num)) {
                    isValid = false;
                    errorMessage = 'Valor numérico inválido';
                } else if (min && num < parseFloat(min)) {
                    isValid = false;
                    errorMessage = `Valor mínimo: ${min}`;
                } else if (max && num > parseFloat(max)) {
                    isValid = false;
                    errorMessage = `Valor máximo: ${max}`;
                }
                break;
        }
        
        // Validações customizadas
        if (field.name === 'ip' && !validateIP(value)) {
            isValid = false;
            errorMessage = 'Endereço IP inválido';
        }
        
        if (field.name === 'port' && !validatePort(parseInt(value))) {
            isValid = false;
            errorMessage = 'Porta deve estar entre 1 e 65535';
        }
    }
    
    // Aplica estilo visual
    if (isValid) {
        field.classList.remove('error');
        field.classList.add('valid');
    } else {
        field.classList.remove('valid');
        field.classList.add('error');
    }
    
    // Exibe/remove mensagem de erro
    showFieldError(field, errorMessage);
    
    return isValid;
}

/**
 * Limpa erro visual de um campo
 * @param {HTMLInputElement} field - Campo
 */
function clearFieldError(field) {
    field.classList.remove('error');
    const errorElement = field.parentNode.querySelector('.field-error');
    if (errorElement) {
        errorElement.remove();
    }
}

/**
 * Exibe erro em um campo
 * @param {HTMLInputElement} field - Campo
 * @param {string} message - Mensagem de erro
 */
function showFieldError(field, message) {
    // Remove erro anterior
    const existingError = field.parentNode.querySelector('.field-error');
    if (existingError) {
        existingError.remove();
    }
    
    // Adiciona novo erro se houver mensagem
    if (message) {
        const errorElement = document.createElement('div');
        errorElement.className = 'field-error';
        errorElement.style.cssText = `
            color: #e74c3c;
            font-size: 12px;
            margin-top: 4px;
            font-weight: 500;
        `;
        errorElement.textContent = message;
        field.parentNode.appendChild(errorElement);
    }
}

/**
 * Valida formulário completo
 * @param {HTMLFormElement} form - Formulário
 * @returns {boolean}
 */
function validateForm(form) {
    const inputs = form.querySelectorAll('input, select, textarea');
    let isFormValid = true;
    
    inputs.forEach(input => {
        if (!validateField(input)) {
            isFormValid = false;
        }
    });
    
    if (!isFormValid) {
        showToast(AppConfig.MESSAGES.VALIDATION_ERROR, 'error');
    }
    
    return isFormValid;
}

/**
 * Toggle visibility of a password input by id
 * @param {string} inputId - id of the password input
 * @param {string} btnId - (optional) id of the toggle button to update its label
 */
function togglePasswordVisibility(inputId, btnId) {
    const input = document.getElementById(inputId);
    if (!input) return;
    if (input.type === 'password') {
        input.type = 'text';
        if (btnId) {
            const b = document.getElementById(btnId); if (b) b.textContent = 'Ocultar';
        }
    } else {
        input.type = 'password';
        if (btnId) {
            const b = document.getElementById(btnId); if (b) b.textContent = 'Mostrar';
        }
    }
}

/**
 * Salva configurações via AJAX
 * @param {HTMLFormElement} form - Formulário
 * @param {string} endpoint - Endpoint para salvamento
 */
function saveConfig(form, endpoint) {
    if (!validateForm(form)) {
        return;
    }
    
    const formData = new FormData(form);
    const submitButton = form.querySelector('button[type="submit"], input[type="submit"]');
    
    if (submitButton) {
        submitButton.disabled = true;
        submitButton.textContent = 'Salvando...';
    }
    
    showToast('Salvando configurações...', 'info');
    
    fetch(endpoint, {
        method: 'POST',
        body: formData
    })
        .then(response => {
            if (response.ok) {
                showToast(AppConfig.MESSAGES.SAVE_SUCCESS, 'success');
                // Redireciona ou atualiza página se necessário
                setTimeout(() => {
                    if (response.redirected) {
                        window.location.href = response.url;
                    }
                }, 1500);
            } else {
                throw new Error('Erro na resposta do servidor');
            }
        })
        .catch(error => {
            console.error('Erro ao salvar:', error);
            showToast(AppConfig.MESSAGES.SAVE_ERROR, 'error');
        })
        .finally(() => {
            if (submitButton) {
                submitButton.disabled = false;
                submitButton.textContent = submitButton.dataset.originalText || 'Salvar';
            }
        });
}

/**
 * ============================================================================
 * INICIALIZAÇÃO E EVENT LISTENERS
 * ============================================================================
 */

/**
 * Inicializa a aplicação quando o DOM está carregado
 */
document.addEventListener('DOMContentLoaded', function() {
    console.log('Sistema de Medição de Combustão - JavaScript carregado');
    
    // Adiciona animação de entrada às páginas
    document.body.style.opacity = '0';
    document.body.style.transition = 'opacity 0.3s ease';
    setTimeout(() => {
        document.body.style.opacity = '1';
    }, 100);
    
    // Configura validação automática em todos os formulários
    const forms = document.querySelectorAll('form');
    forms.forEach(form => {
        setupFormValidation(form);
        
        // Salva texto original dos botões
        const submitButton = form.querySelector('button[type="submit"], input[type="submit"]');
        if (submitButton) {
            submitButton.dataset.originalText = submitButton.textContent;
        }
    });
    
    // Event listeners para elementos específicos
    setupEventListeners();
    
    // Adiciona estilos CSS para campos com erro/sucesso
    addValidationStyles();
});

/**
 * Configura event listeners específicos
 */
function setupEventListeners() {
    // Botão de scan Wi-Fi
    const scanWifiBtn = document.querySelector('#scan-wifi-btn');
    if (scanWifiBtn) {
        scanWifiBtn.addEventListener('click', scanWifiNetworks);
    }
    
    // Formulário de login
    const loginForm = document.querySelector('#login-form');
    if (loginForm) {
        // Prevent the form from being submitted by pressing Enter in any field
        loginForm.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                // prevent default Enter behavior inside the form
                e.preventDefault();
            }
        });

        // Submit only when the explicit login button is clicked
        const loginBtn = document.querySelector('#login-btn');
        if (loginBtn) {
            loginBtn.addEventListener('click', function() {
                if (validateForm(loginForm)) {
                    // Use the form's submit to follow the action/method
                    loginForm.submit();
                }
            });
        }
    }
    
    // Formulários de configuração
    const configForms = document.querySelectorAll('.config-form');
    configForms.forEach(form => {
        form.addEventListener('submit', function(e) {
            e.preventDefault();
            const endpoint = this.getAttribute('action') || '/save_config';
            saveConfig(this, endpoint);
        });
    });
    
    // Toggle de modo Modbus (RTU/TCP)
    const modbusRadios = document.querySelectorAll('input[name="modbus_mode"]');
    modbusRadios.forEach(radio => {
        radio.addEventListener('change', toggleModbusMode);
    });
    
    // Auto-atualização de status
    if (document.querySelector('#wifi-status, #system-status')) {
        startStatusUpdates();
    }
}

/**
 * Alterna modo Modbus (RTU/TCP)
 */
function toggleModbusMode() {
    const selectedMode = document.querySelector('input[name="modbus_mode"]:checked')?.value;
    const rtuBlock = document.querySelector('#modbus-rtu-config');
    const tcpBlock = document.querySelector('#modbus-tcp-config');
    
    if (rtuBlock && tcpBlock) {
        if (selectedMode === 'rtu') {
            rtuBlock.style.display = 'block';
            tcpBlock.style.display = 'none';
        } else if (selectedMode === 'tcp') {
            rtuBlock.style.display = 'none';
            tcpBlock.style.display = 'block';
        }
    }
}

/**
 * Inicia atualizações automáticas de status
 */
function startStatusUpdates() {
    // Atualiza status a cada 30 segundos
    setInterval(() => {
        updateSystemStatus();
    }, 30000);
    
    // Primeira atualização imediata
    updateSystemStatus();
}

/**
 * Atualiza status do sistema
 */
function updateSystemStatus() {
    fetch('/wifi_status')
        .then(response => response.json())
        .then(data => {
            const statusElement = document.querySelector('#wifi-status');
            if (statusElement) {
                statusElement.innerHTML = `
                    <strong>Status Wi-Fi:</strong> ${data.connected ? 'Conectado' : 'Desconectado'}<br>
                    ${data.connected ? `<strong>IP:</strong> ${data.ip}<br><strong>SSID:</strong> ${data.ssid}` : ''}
                `;
            }
        })
        .catch(error => {
            console.error('Erro ao atualizar status:', error);
        });
}

/**
 * Adiciona estilos CSS para validação de campos
 */
function addValidationStyles() {
    const style = document.createElement('style');
    style.textContent = `
        .error {
            border-color: #e74c3c !important;
            box-shadow: 0 0 5px rgba(231, 76, 60, 0.3) !important;
        }
        
        .valid {
            border-color: #27ae60 !important;
            box-shadow: 0 0 5px rgba(39, 174, 96, 0.3) !important;
        }
        
        .field-error {
            animation: slideDown 0.3s ease;
        }
        
        @keyframes slideDown {
            from {
                opacity: 0;
                transform: translateY(-10px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        
        @keyframes slideInRight {
            from {
                opacity: 0;
                transform: translateX(100%);
            }
            to {
                opacity: 1;
                transform: translateX(0);
            }
        }
    `;
    document.head.appendChild(style);
}

/**
 * ============================================================================
 * FUNÇÕES UTILITÁRIAS PARA DESENVOLVIMENTO
 * ============================================================================
 */

/**
 * Log de debug (apenas em desenvolvimento)
 * @param {*} data - Dados para log
 */
function debugLog(data) {
    if (window.location.hostname === 'localhost' || window.location.hostname.includes('192.168')) {
        console.log('[DEBUG]', data);
    }
}

/**
 * Exporta funções para uso global (se necessário)
 */
window.AppUtils = {
    showToast,
    validateEmail,
    validateIP,
    validatePort,
    sanitizeInput,
    doFactoryReset,
    scanWifiNetworks,
    testWifiConnection,
    saveConfig,
    debugLog
};