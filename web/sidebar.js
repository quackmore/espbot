// shared.js
const esp8266 = {
  "url": "",
  "cors": false
  // "url": "http://192.168.1.187",
  // "url": "http://192.168.100.100",
  // "url": "http://192.168.10.1",
  // "cors": true
};

function update_sidebar() {
  $('#app_settings').hide();
}

function ajax_error(xhr, status) {
  if (status === "timeout") {
    alert("Ajax timeout!");
  } else {
    if (xhr.responseText !== undefined) {
      var answer = JSON.parse(xhr.responseText);
      alert("" + answer.error.reason);
    }
    else {
      alert("Ajax error!");
    }
  }
  setTimeout(function () {
    $('#awaiting').modal('hide');
  }, 1000);
}

function goto(page) {
  $('#awaiting').modal('show');
  $("#page-content").load(page + ".html");
}