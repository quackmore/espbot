const esp8266 = {
  //  "url": "",
  //  "cors": false
  "url": "http://192.168.1.229",
  "cors": true
};

$(document).ready(function () {
  update_home();
});

function update_home() {
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/info',
    dataType: 'json',
    crossDomain: esp8266.cors,
    success: function (data) {
      $("#esp_name").text(function () { return data.app_name + " (on device " + data.espbot_name + ")" });
      $("#app_name").val(data.app_name);
      $("#app_version").val(data.app_version);
      $("#dev_name").val(data.espbot_name);
      $("#espbot_version").val(data.espbot_version);
      $("#library_version").val(data.library_version);
      $("#chip_id").val(data.chip_id);
      $("#sdk_version").val(data.sdk_version);
      $("#boot_version").val(data.boot_version);
    },
    error: function (xhr) {
      alert("update_home got: " + xhr.responseText);
    }
  });
}

$('#change_name').on('click', function (e) {
  $.ajax({
    type: 'POST',
    url: esp8266.url + '/api/espbot/cfg',
    dataType: 'json',
    contentType: 'application/json',
    data: JSON.stringify({ espbot_name: $('#dev_name').val() }),
    crossDomain: esp8266.cors,
    success: function () {
      update_home();
    },
    error: function (xhr) {
      alert("#change_name on 'click' got: " + xhr.responseText);
    }
  })
});

function update_wifi() {
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/wifi/info',
    dataType: 'json',
    crossDomain: esp8266.cors,
    success: function (data) {
      $("#wifi_working_as").val(data.op_mode);
      $("#wifi_ssid_label").text(function () {
        if (data.op_mode === "STATION")
          return "Connected to";
        else
          return "SSID";
      });
      $("#wifi_ssid").val(data.SSID);
      $("#wifi_IP").val(data.ip_address);
    },
    error: function (xhr) {
      alert("update_wifi got: " + xhr.responseText);
    }
  });
}

function update_wifi_aps() {
  $("#wifi_aps").empty();
  $("#wifi_aps").append('<option selected><span class="sr-only">Loading...</span></option>');
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/wifi/scan',
    dataType: 'json',
    crossDomain: esp8266.cors,
    success: function (data) {
      $("#wifi_aps").empty();
      for (var ii = 0; ii < data.AP_SSIDs.length; ii++) {
        if ($("#wifi_ssid").val() === data.AP_SSIDs[ii]) {
          $("#wifi_aps").append("<option value='" + ii + "' selected>" + data.AP_SSIDs[ii] + "</option>");
        }
        else {
          $("#wifi_aps").append("<option value='" + ii + "'>" + data.AP_SSIDs[ii] + "</option>");
        }
      }
    },
    error: function (xhr) {
      alert("update_wifi_aps got: " + xhr.responseText);
    }
  });
}

$('#wifi_refresh').on('click', function (e) {
  update_wifi_aps();
});

$('#wifi_connect').on('click', function (e) {
  $.ajax({
    type: 'POST',
    url: esp8266.url + '/api/wifi/cfg',
    dataType: 'json',
    contentType: 'application/json',
    data: JSON.stringify({ station_ssid: $('#wifi_aps option:selected').text(), station_pwd: $('#wifi_pwd').val() }),
    crossDomain: esp8266.cors,
    success: function () {
      update_home();
    },
    error: function (xhr) {
      alert("#change_name on 'click' got: " + xhr.responseText);
    }
  })
});

function update_ota() {
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/ota/cfg',
    dataType: 'json',
    crossDomain: esp8266.cors,
    success: function (data) {
      $("#ota_host").val(data.host);
      $("#ota_port").val(data.port);
      $("#ota_path").val(data.path);
      if (eval(data.check_version)) {
        $("#ota_cv").prop('checked', true);
      }
      else {
        $("#ota_cv").prop('checked', false);
      }
      if (eval(data.reboot_on_completion)) {
        $("#ota_rac").prop('checked', true);
      }
      else {
        $("#ota_rac").prop('checked', false);
      }
    },
    error: function (xhr) {
      alert("update_ota got: " + xhr.responseText);
    }
  });
}

$('#ota_refresh').on('click', function (e) {
  update_ota();
});

$('#ota_save').on('click', function (e) {
  $.ajax({
    type: 'POST',
    url: esp8266.url + '/api/ota/cfg',
    dataType: 'json',
    contentType: 'application/json',
    data: JSON.stringify({
      host: $('#ota_host').val(),
      port: Number($('#ota_port').val()),
      path: $('#ota_path').val(),
      check_version: $('#ota_cv').prop('checked').toString(),
      reboot_on_completion: $('#ota_rac').prop('checked').toString()
    }),
    crossDomain: esp8266.cors,
    success: function () {
    },
    error: function (xhr) {
      alert("#ota_save on 'click' got: " + xhr.responseText);
    }
  })
});

$('#ota_upgrade').on('click', function (e) {
  $.ajax({
    type: 'POST',
    url: esp8266.url + '/api/ota/upgrade',
    contentType: 'text/html',
    data: '',
    crossDomain: esp8266.cors,
    success: function () {
    },
    error: function (xhr) {
      alert("#ota_upgrade on 'click' got: " + xhr.responseText);
    }
  })
});

$('a[data-toggle="tab"]').on('shown.bs.tab', function (e) {
  var target = $(e.target).attr("href");
  switch (target) {
    case "#home":
      update_home();
      break;
    case "#WiFi":
      update_wifi();
      update_wifi_aps();
      break;
    case "#OTA":
      update_ota();
      break;
    case "#debug_lev":
      break;
    case "#debug_log":
      break;
    case "#debug_mem":
      break;
    case "#dataLog":
      update_diag_events();
      break;
    default:
      alert("Ops... there is a wrong pane href ...");
  }
});

// DIAGNOSTIC EVENTS
$('#diag_evnt_acknowledge').on('click', function () {
  esp_ack_diag_events(function (xhr) {
    alert("" + xhr);
  }, function (xhr) {
    alert("" + JSON.parse(xhr.responseText).error.reason);
  });
  update_diag_events();
});

$('#diag_evnt_log_refresh').on('click', function () {
  update_diag_events();
});

function esp_ack_diag_events(success_cb, error_cb) {
  $.ajax({
    type: 'POST',
    url: esp8266.url + '/api/diag/ack_events',
    crossDomain: esp8266.cors,
    success: function (xhr) {
    },
    error: function (xhr) {
      error_cb(xhr);
    }
  })
}

function esp_get_diag_events(success_cb, error_cb) {
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/diag/events',
    dataType: 'json',
    crossDomain: esp8266.cors,
    //success: success_cb(data),
    success: function (data) {
      success_cb(data);
    },
    error: function (xhr) {
      error_cb(xhr);
    }
  });
}

function update_diag_events() {
  esp_get_diag_events(update_diag_events_table, function (xhr) {
    alert("" + JSON.parse(xhr.responseText).error.reason);
  });
}

function get_diag_events(success_cb, error_cb) {
  $.ajax({
    type: 'GET',
    url: esp8266.url + '/api/diag/events',
    dataType: 'json',
    crossDomain: esp8266.cors,
    //success: success_cb(data),
    success: function (data) {
      success_cb(data);
    },
    error: function (xhr) {
      error_cb(xhr);
    }
  });
}

function update_diag_events_table(data) {
  $("#diag_event_table").empty();
  $("#diag_event_table").append('<thead><tr><th scope="col">Timestamp</th><th scope="col">Type</th><th scope="col">Code</th><th scope="col">Desc</th><th scope="col">Value</th></tr></thead><tbody>');
  for (var ii = 0; ii < data.diag_events.length; ii++) {
    let ts = new Date(data.diag_events[ii].ts * 1000);
    if (data.diag_events[ii].ack == 1) {
      $("#diag_event_table").append('<tr class="text-success"><td>' + ts.toString().substring(4, 24) + '</td><td>' + get_evnt_str(data.diag_events[ii].type) + '</td><td>' + String("0000" + data.diag_events[ii].code).slice(-4) + '</td><td>' + get_code_str(data.diag_events[ii].code) + '</td><td>' + data.diag_events[ii].val + '</td></tr>');
    }
    else {
      $("#diag_event_table").append('<tr><td>' + ts.toString().substring(4, 24) + '</td><td>' + get_evnt_str(data.diag_events[ii].type) + '</td><td>' + String("0000" + data.diag_events[ii].code).slice(-4) + '</td><td>' + get_code_str(data.diag_events[ii].code) + '</td><td>' + data.diag_events[ii].val + '</td></tr>');
    }
  }
  $("#diag_event_table").append('</tbody>');
}

function get_evnt_str(type) {
  var evnt_str = [];
  evnt_str[parseInt(1, 16)] = "FATAL";
  evnt_str[parseInt(2, 16)] = "ERROR";
  evnt_str[parseInt(4, 16)] = "WARN";
  evnt_str[parseInt(8, 16)] = "INFO";
  evnt_str[parseInt(10, 16)] = "DEBUG";
  evnt_str[parseInt(20, 16)] = "TRACE";
  return evnt_str[parseInt(type, 16)];
}