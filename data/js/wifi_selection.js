// WiFi Selection Logic
(function(){
    var radios = document.querySelectorAll('input[name="ssid_radio"]');
    var ssidInput = document.getElementById('ssid_input');
    
    function updateSelectedClasses(){ 
        var items = document.querySelectorAll('.ap-item'); 
        for(var j=0; j<items.length; j++){ 
            items[j].classList.remove('selected'); 
            var r = items[j].querySelector('input[name="ssid_radio"]'); 
            if(r && r.checked) {
                items[j].classList.add('selected'); 
            }
        } 
    }
    
    // Add change listeners to radio buttons
    for(var i=0; i<radios.length; i++){ 
        (function(r){ 
            r.addEventListener('change', function(){ 
                if(ssidInput) {
                    ssidInput.value = this.value; 
                }
                updateSelectedClasses(); 
            }); 
        })(radios[i]); 
    }
    
    // Set initial selection
    if(ssidInput && radios.length > 0 && (!ssidInput.value || ssidInput.value.length === 0)){ 
        ssidInput.value = radios[0].value; 
        radios[0].checked = true; 
        updateSelectedClasses();
    }
})();