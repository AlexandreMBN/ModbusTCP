// Funções para configuração do Access Point
document.addEventListener('DOMContentLoaded', function() {
    // Validação em tempo real
    setupValidation();
});

function setupValidation() {
    const form = document.getElementById('ap-config-form');
    const ssidInput = document.getElementById('ap-ssid');
    const passwordInput = document.getElementById('ap-password');
    const ipInput = document.getElementById('ap-ip');
    
    // Validação do SSID
    ssidInput.addEventListener('input', function() {
        validateSSID(this);
    });
    
    // Validação da senha
    passwordInput.addEventListener('input', function() {
        validatePassword(this);
    });
    
    // Validação do IP
    ipInput.addEventListener('input', function() {
        validateIP(this);
    });
    
    // Submissão do formulário
    form.addEventListener('submit', function(e) {
        e.preventDefault();
        
        if (validateForm()) {
            submitAPConfig();
        }
    });
}

function validateSSID(input) {
    const value = input.value.trim();
    const isValid = value.length >= 1 && value.length <= 32;
    
    setInputValidation(input, isValid, 
        isValid ? '' : 'SSID deve ter entre 1 e 32 caracteres');
    
    return isValid;
}

function validatePassword(input) {
    const value = input.value;
    const isValid = value.length >= 8 && value.length <= 63;
    
    setInputValidation(input, isValid, 
        isValid ? '' : 'Senha deve ter entre 8 e 63 caracteres');
    
    return isValid;
}

function validateIP(input) {
    const value = input.value.trim();
    const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
    let isValid = ipRegex.test(value);
    
    if (isValid) {
        // Verifica se cada octeto está no range correto
        const octets = value.split('.');
        isValid = octets.every(octet => {
            const num = parseInt(octet);
            return num >= 0 && num <= 255;
        });
    }
    
    setInputValidation(input, isValid, 
        isValid ? '' : 'IP deve estar no formato válido (ex: 192.168.4.1)');
    
    return isValid;
}

function setInputValidation(input, isValid, message) {
    const parent = input.parentElement;
    let errorElement = parent.querySelector('.validation-error');
    
    if (!isValid) {
        if (!errorElement) {
            errorElement = document.createElement('div');
            errorElement.className = 'validation-error';
            parent.appendChild(errorElement);
        }
        errorElement.textContent = message;
        input.classList.add('error');
    } else {
        if (errorElement) {
            errorElement.remove();
        }
        input.classList.remove('error');
    }
}

function validateForm() {
    const ssidInput = document.getElementById('ap-ssid');
    const passwordInput = document.getElementById('ap-password');
    const ipInput = document.getElementById('ap-ip');
    
    const ssidValid = validateSSID(ssidInput);
    const passwordValid = validatePassword(passwordInput);
    const ipValid = validateIP(ipInput);
    
    return ssidValid && passwordValid && ipValid;
}

function submitAPConfig() {
    const form = document.getElementById('ap-config-form');
    const formData = new FormData(form);
    const data = new URLSearchParams(formData);
    
    // Desabilita o botão durante o envio
    const submitBtn = form.querySelector('button[type="submit"]');
    const originalText = submitBtn.textContent;
    submitBtn.disabled = true;
    submitBtn.textContent = 'Salvando...';
    
    fetch('/ap-save', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: data
    })
    .then(response => response.text())
    .then(result => {
        alert('Configuração salva com sucesso!\nO ESP32 será reiniciado para aplicar as mudanças.');
        // Redireciona para a página principal após um breve delay
        setTimeout(() => {
            window.location.href = '/';
        }, 2000);
    })
    .catch(error => {
        console.error('Erro ao salvar configuração:', error);
        alert('Erro ao salvar configuração. Tente novamente.');
        
        // Reabilita o botão
        submitBtn.disabled = false;
        submitBtn.textContent = originalText;
    });
}

function resetToDefaults() {
    if (confirm('Restaurar configurações padrão?\n\nSSID: ESP32-AP\nUsuário: admin\nSenha: 12345678\nIP: 192.168.4.1')) {
        document.getElementById('ap-ssid').value = 'ESP32-AP';
        document.getElementById('ap-username').value = 'admin';
        document.getElementById('ap-password').value = '12345678';
        document.getElementById('ap-ip').value = '192.168.4.1';
        
        // Remove qualquer validação de erro anterior
        const errorElements = document.querySelectorAll('.validation-error');
        errorElements.forEach(element => element.remove());
        
        const inputElements = document.querySelectorAll('input.error');
        inputElements.forEach(element => element.classList.remove('error'));
    }
}