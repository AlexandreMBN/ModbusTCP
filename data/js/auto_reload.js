// Auto-reload page every 1 second during WiFi scanning
document.addEventListener('DOMContentLoaded', function() {
    // Only auto-reload when the WiFi scan UI is present
    if (document.querySelector('#scan-wifi-btn') || window.location.pathname.includes('wifi_scanning') || window.location.pathname.includes('wifi-scan')) {
        setTimeout(function(){
            location.reload();
        }, 1000);
    }
});