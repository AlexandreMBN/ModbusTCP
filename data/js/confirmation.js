// Script para página de confirmação com redirecionamento automático
document.addEventListener('DOMContentLoaded', function() {
    const countdownElement = document.getElementById('countdown');
    const redirectDiv = document.getElementById('auto-redirect');
    
    if (countdownElement && redirectDiv && redirectDiv.style.display !== 'none') {
        let countdown = parseInt(countdownElement.textContent);
        
        if (countdown > 0) {
            const interval = setInterval(function() {
                countdown--;
                countdownElement.textContent = countdown;
                
                if (countdown <= 0) {
                    clearInterval(interval);
                    // Redireciona usando a URL do botão principal
                    const returnButton = document.querySelector('.btn-primary');
                    if (returnButton && returnButton.href) {
                        window.location.href = returnButton.href;
                    } else {
                        window.location.href = '/';
                    }
                }
            }, 1000);
        }
    }
});