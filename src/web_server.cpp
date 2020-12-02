#include <WebServer.h>


static WebServer server(80);


 
static String updateIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";



static void handleNotFound()
{
//	if(loadFromFS(server.uri())) return;
	String message = F("Not Found\n\n");
	message += String(F("URI: "));
	message += server.uri();
	message += String(F("\nMethod: "));
	message += (server.method() == HTTP_GET)?F("GET"):F("POST");
	message += String(F("\nArguments: "));
	message += server.args();
	message += String(F("\n"));
	for (uint8_t i=0; i<server.args(); i++){
		message += String(F(" NAME:"))+server.argName(i) + F("\n VALUE:") + server.arg(i) + F("\n");
	}
	Serial.print(message);
	server.send(404, F("text/plain"), message);

}

void web_server_setup()
{
	// setup handlers

	server.on(F("/"), HTTP_GET, []() {
			String html = R"HTMLTEXT(

			<html><body>
			<h1>MZ5</h1>


			</body></html>

			)HTMLTEXT";

			server.send(200,  F("text/html"), html		);
	});


	server.on("/update", HTTP_GET, []() {
		server.sendHeader("Connection", "close");
		server.send(200, "text/html", updateIndex);
	});

	server.on("/update", HTTP_POST, []() {
		server.sendHeader("Connection", "close");
		server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
		ESP.restart();
	  }, []() {
		HTTPUpload& upload = server.upload();
		if (upload.status == UPLOAD_FILE_START) {
		  printf("Update: %s\n", upload.filename.c_str());
//		  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
//		    Update.printError(Serial);
		  }
		} else if (upload.status == UPLOAD_FILE_WRITE) {
		  /* flashing firmware to ESP*/
//		  if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
//		    Update.printError(Serial);
		  }
		} else if (upload.status == UPLOAD_FILE_END) {
//		  if (Update.end(true)) { //true to set the size to the current progress
//		    Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
//		  } else {
//		    Update.printError(Serial);
//		  }
		}
	  });


	server.begin();
	puts("HTTP server started");

}

void web_server_handle_client()
{
	server.handleClient();
}
