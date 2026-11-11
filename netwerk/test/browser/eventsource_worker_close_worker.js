"use strict";

onmessage = ({ data: { url, close } }) => {
  const es = new EventSource(url);
  es.onmessage = () => {
    es.onmessage = null;
    if (close) {
      es.close();
    }
    postMessage("message");
  };
  es.onerror = () => {
    es.close();
    postMessage("error");
  };
};
