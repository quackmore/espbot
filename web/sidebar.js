// shared.js
var esp8266 = {
  "name": "Espbot",
  "type": "ESPBOT",
  "api": "2.2.0",
  "ip": "192.168.10.1",
  "url": "",
  "cors": false
  // "url": "http://192.168.1.187",
  // "cors": true
};

function goto(page) {
  if ((window.matchMedia("(max-width: 768px)")).matches)
    $("#wrapper").removeClass("toggled");
  $('#awaiting').modal('show');
  $("#page-content").load(page + ".html", function (responseText, textStatus, xhr) {
    if (textStatus != "success")
      query_err(textStatus, xhr);
  });
}

// common functions

function show_spinner() {
  return new Promise(function (resolve) {
    $('#awaiting').modal('show');
    resolve("spinner shown");
  });
}

function hide_spinner(timeout) {
  setTimeout(function () {
    $('#awaiting').modal('hide');
  }, timeout);
}

function query_err(status, xhr) {
  if (status === "timeout") {
    alert("Request timeout!");
    hide_spinner(500);
  } else {
    if (xhr.responseText !== undefined) {
      var answer = JSON.parse(xhr.responseText);
      alert("" + answer.error.reason);
      hide_spinner(500);
    }
    else {
      alert("Device unreachable!");
      hide_spinner(500);
    }
  }
}

function esp_query(query) {
  if (!query.hasOwnProperty('timeout'))
    query.timeout = 4000;
  return new Promise(function (resolve, reject) {
    $.ajax({
      type: query.type,
      url: esp8266.url + query.url,
      dataType: query.dataType,
      contentType: query.contentType,
      data: query.data,
      processData: query.processData,
      crossDomain: esp8266.cors,
      timeout: query.timeout,
      success: function (data) {
        if (query.success)
          query.success(data);
        resolve(data);
      },
      error: function (jqXHR, textStatus) {
        if (query.error)
          query.error(jqXHR, textStatus);
        reject(jqXHR, textStatus);
      }
    });
  });
}
